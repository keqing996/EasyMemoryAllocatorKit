#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include "MAFConfig.hpp"

namespace MAF
{
    /// @brief Two-Level Segregated Fit allocator with O(1) alloc/free.
    /// @details Uses first-level and second-level bitmaps to locate free blocks in constant time.
    ///          Supports coalescing and arbitrary alignment. Ideal for real-time and
    ///          latency-sensitive workloads. Not thread-safe.
    /// @tparam FL_COUNT Number of first-level buckets (default 16).
    /// @tparam SL_COUNT Number of second-level buckets per FL (default 16, must be power of 2).
    /// @param size Total arena size in bytes.
    /// @param defaultAlignment Default alignment (must be power of 2).
    /// @throws std::invalid_argument If size is too small or alignment is invalid.
    template<size_t FL_COUNT = 16, size_t SL_COUNT = 16>
    class TLSFAllocator
    {
    private:
        static constexpr bool IsPowerOfTwoConstexpr(size_t value)
        {
            return value > 0 && (value & (value - 1)) == 0;
        }

        using FlBitmap = uint32_t;
        using SlBitmap = uint32_t;
        static constexpr size_t BITMAP_BITS = std::numeric_limits<FlBitmap>::digits;

        static_assert(IsPowerOfTwoConstexpr(FL_COUNT) && FL_COUNT >= 4,
            "FL_COUNT must be a power of 2 and >= 4");
        static_assert(IsPowerOfTwoConstexpr(SL_COUNT) && SL_COUNT >= 4,
            "SL_COUNT must be a power of 2 and >= 4");
        static_assert(FL_COUNT <= BITMAP_BITS,
            "FL_COUNT exceeds the 32-bit first-level bitmap capacity");
        static_assert(SL_COUNT <= BITMAP_BITS,
            "SL_COUNT exceeds the 32-bit second-level bitmap capacity");

    private:
        class BlockHeader
        {
        public:
            void Initialize(size_t size, bool used, BlockHeader* prevPhysical)
            {
                _pPrevPhysical = prevPhysical;
                _usedAndSize = size & ~HIGHEST_BIT_MASK;
                if (used)
                    _usedAndSize |= HIGHEST_BIT_MASK;
                _pNextFree = nullptr;
                _pPrevFree = nullptr;
            }

            size_t GetSize() const
            {
                return _usedAndSize & ~HIGHEST_BIT_MASK;
            }

            void SetSize(size_t size)
            {
                _usedAndSize = (_usedAndSize & HIGHEST_BIT_MASK) | (size & ~HIGHEST_BIT_MASK);
            }

            bool IsUsed() const
            {
                return (_usedAndSize & HIGHEST_BIT_MASK) != 0;
            }

            void SetUsed(bool used)
            {
                if (used)
                    _usedAndSize |= HIGHEST_BIT_MASK;
                else
                    _usedAndSize &= ~HIGHEST_BIT_MASK;
            }

            BlockHeader* GetPrevPhysical() const
            {
                return _pPrevPhysical;
            }

            void SetPrevPhysical(BlockHeader* prev)
            {
                _pPrevPhysical = prev;
            }

            BlockHeader* GetNextFree() const
            {
                return _pNextFree;
            }

            void SetNextFree(BlockHeader* next)
            {
                _pNextFree = next;
            }

            BlockHeader* GetPrevFree() const
            {
                return _pPrevFree;
            }

            void SetPrevFree(BlockHeader* prev)
            {
                _pPrevFree = prev;
            }

            void ClearFreeLinks()
            {
                _pNextFree = nullptr;
                _pPrevFree = nullptr;
            }

            void ClearData()
            {
                Initialize(0, false, nullptr);
            }

        private:
            BlockHeader* _pPrevPhysical;
            size_t _usedAndSize;
            BlockHeader* _pNextFree;
            BlockHeader* _pPrevFree;
        };

        using FreeListArray = std::array<std::array<BlockHeader*, SL_COUNT>, FL_COUNT>;

    public:
        explicit TLSFAllocator(size_t size, size_t defaultAlignment = alignof(std::max_align_t))
            : _pData(nullptr)
            , _size(size)
            , _defaultAlignment(defaultAlignment)
            , _pFirstBlock(nullptr)
            , _flBitmap(0)
            , _slBitmaps{}
            , _freeLists{}
        {
            if (!IsPowerOfTwo(defaultAlignment))
                throw std::invalid_argument("TLSFAllocator defaultAlignment must be a power of 2");

            _defaultAlignment = std::max(_defaultAlignment, MIN_DEFAULT_ALIGNMENT);

            size_t minimumPoolPayload = 0;
            if (!TryCalculateWorstCaseRequiredSpace(1, _defaultAlignment, minimumPoolPayload))
                throw std::invalid_argument("TLSFAllocator defaultAlignment is too large");

            size_t minimumPoolSize = 0;
            if (!TryAdd(sizeof(BlockHeader), minimumPoolPayload, minimumPoolSize))
                throw std::invalid_argument("TLSFAllocator size overflow");

            if (_size < minimumPoolSize)
                _size = minimumPoolSize;

            if (_size <= sizeof(BlockHeader) || (_size - sizeof(BlockHeader)) > MAX_BLOCK_SIZE)
                throw std::invalid_argument("TLSFAllocator size exceeds representable block capacity");

            _pData = static_cast<uint8_t*>(::malloc(_size));
            if (!_pData)
                throw std::bad_alloc();

            InitializeMemoryPool();
        }

        ~TLSFAllocator()
        {
            if (_pData)
            {
                ::free(_pData);
                _pData = nullptr;
            }
        }

        TLSFAllocator(const TLSFAllocator& rhs) = delete;
        TLSFAllocator(TLSFAllocator&& rhs) = delete;
        TLSFAllocator& operator=(const TLSFAllocator&) = delete;
        TLSFAllocator& operator=(TLSFAllocator&&) = delete;

        void* Allocate(size_t size)
        {
            return Allocate(size, _defaultAlignment);
        }

        void* Allocate(size_t size, size_t alignment)
        {
            if (size == 0)
                return nullptr;

            if (!IsPowerOfTwo(alignment))
                throw std::invalid_argument("TLSFAllocator only supports power-of-2 alignments");

            size_t searchStartSize = 0;
            if (!TryCalculateSearchStartSize(size, searchStartSize))
                return nullptr;

            size_t fl = 0;
            size_t sl = 0;
            MappingSearch(searchStartSize, fl, sl);

            size_t alignedUserAddr = 0;
            size_t payloadNeeded = 0;
            BlockHeader* block = SearchSuitableBlock(
                fl, sl, size, alignment, alignedUserAddr, payloadNeeded);
            if (!block)
                return nullptr;

            RemoveFromFreeList(block);

            size_t consumedPayload = 0;
            if (!TryGetConsumablePayloadSize(block, payloadNeeded, consumedPayload))
            {
                InsertIntoFreeList(block);
                return nullptr;
            }

            SplitBlock(block, consumedPayload);
            block->SetUsed(true);

            void* pAlignedUserData = reinterpret_cast<void*>(alignedUserAddr);
            StoreDistance(pAlignedUserData, alignedUserAddr - ToAddr(block));
            return pAlignedUserData;
        }

        void Deallocate(void* p)
        {
            if (!p)
                return;

            BlockHeader* block = ValidateAndGetHeaderFromUserPtr(p);
            if (!block)
                return;

            block->SetUsed(false);

            block = MergeWithNext(block);
            block = MergeWithPrev(block);

            InsertIntoFreeList(block);
        }

        void* GetMemoryBlockPtr() const
        {
            return _pData;
        }

        BlockHeader* GetFirstBlock() const
        {
            return _pFirstBlock;
        }

        size_t GetCapacity() const
        {
            return _size;
        }

        size_t GetUsedSpace() const
        {
            size_t used = 0;
            BlockHeader* block = _pFirstBlock;
            const uintptr_t endAddr = ToAddr(_pData) + _size;
            while (block && ToAddr(block) < endAddr)
            {
                if (block->IsUsed())
                    used += sizeof(BlockHeader) + block->GetSize();
                block = GetNextPhysicalBlock(block);
            }
            return used;
        }

        size_t GetFreeSpace() const
        {
            return _size - GetUsedSpace();
        }

    private:
        void InitializeMemoryPool()
        {
            _pFirstBlock = reinterpret_cast<BlockHeader*>(_pData);
            _pFirstBlock->Initialize(_size - sizeof(BlockHeader), false, nullptr);

            for (auto& slArray : _freeLists)
            {
                for (auto& blockPtr : slArray)
                    blockPtr = nullptr;
            }

            InsertIntoFreeList(_pFirstBlock);
        }

        bool TryCalculateWorstCaseRequiredSpace(
            size_t size,
            size_t alignment,
            size_t& requiredSpace) const
        {
            requiredSpace = 0;

            if (size == 0 || size > MAX_BLOCK_SIZE)
                return false;

            if (!IsPowerOfTwo(alignment))
                return false;

            size_t totalSpace = 0;
            if (!TryAdd(size, alignment - 1, totalSpace))
                return false;
            if (!TryAdd(totalSpace, DISTANCE_STORAGE_SIZE, totalSpace))
                return false;

            requiredSpace = std::max(totalSpace, MIN_ALLOCATABLE_PAYLOAD);
            return requiredSpace <= MAX_BLOCK_SIZE;
        }

        bool TryCalculateSearchStartSize(size_t size, size_t& searchStartSize) const
        {
            searchStartSize = 0;

            if (size == 0 || size > MAX_BLOCK_SIZE)
                return false;

            if (!TryAdd(size, DISTANCE_STORAGE_SIZE, searchStartSize))
                return false;

            searchStartSize = std::max(searchStartSize, MIN_ALLOCATABLE_PAYLOAD);
            return searchStartSize <= MAX_BLOCK_SIZE;
        }

        bool TryGetAllocationLayout(
            BlockHeader* block,
            size_t size,
            size_t alignment,
            size_t& alignedUserAddr,
            size_t& payloadNeeded) const
        {
            alignedUserAddr = 0;
            payloadNeeded = 0;

            size_t afterHeaderAddr = 0;
            if (!TryAdd(ToAddr(block), sizeof(BlockHeader), afterHeaderAddr))
                return false;

            size_t minimumUserAddr = 0;
            if (!TryAdd(afterHeaderAddr, DISTANCE_STORAGE_SIZE, minimumUserAddr))
                return false;

            if (!TryAlignUp(minimumUserAddr, alignment, alignedUserAddr))
                return false;

            size_t padding = alignedUserAddr - afterHeaderAddr;
            if (!TryAdd(padding, size, payloadNeeded))
                return false;

            return payloadNeeded <= block->GetSize();
        }

        bool TryGetConsumablePayloadSize(
            const BlockHeader* block,
            size_t payloadNeeded,
            size_t& consumedPayload) const
        {
            consumedPayload = 0;

            if (payloadNeeded > block->GetSize())
                return false;

            consumedPayload = block->GetSize();

            size_t splitPayload = 0;
            if (!TryAlignUp(payloadNeeded, BLOCK_GRANULARITY, splitPayload))
                return false;

            if (splitPayload > block->GetSize())
                return true;

            const size_t remainingSize = block->GetSize() - splitPayload;
            if (remainingSize >= sizeof(BlockHeader) + MIN_ALLOCATABLE_PAYLOAD)
                consumedPayload = splitPayload;

            return true;
        }

        void MappingInsert(size_t size, size_t& fl, size_t& sl) const
        {
            if (size < SMALL_BLOCK_SIZE)
            {
                fl = 0;
                sl = std::min((size - 1) / SMALL_BLOCK_STEP, SL_COUNT - 1);
                return;
            }

            const size_t rawFl = Log2(size);
            if (rawFl >= FL_COUNT)
            {
                fl = FL_COUNT - 1;
                sl = SL_COUNT - 1;
                return;
            }

            fl = rawFl;

            const size_t baseSize = size_t(1) << rawFl;
            size_t stepSize = baseSize >> Log2(SL_COUNT);
            if (stepSize == 0)
                stepSize = 1;

            sl = (size - baseSize) / stepSize;
            if (sl >= SL_COUNT)
                sl = SL_COUNT - 1;
        }

        void MappingSearch(size_t size, size_t& fl, size_t& sl) const
        {
            MappingInsert(size, fl, sl);
        }

        // This allocator uses TLSF-style bins as a search hint, then validates
        // the exact aligned layout against each candidate block.
        BlockHeader* SearchSuitableBlock(
            size_t fl,
            size_t sl,
            size_t size,
            size_t alignment,
            size_t& alignedUserAddr,
            size_t& payloadNeeded) const
        {
            alignedUserAddr = 0;
            payloadNeeded = 0;

            for (size_t currentFl = fl; currentFl < FL_COUNT; ++currentFl)
            {
                if ((_flBitmap & MakeBitmapMask(currentFl)) == 0)
                    continue;

                size_t startSl = currentFl == fl ? sl : 0;
                SlBitmap availableSl = _slBitmaps[currentFl] & (~SlBitmap(0) << startSl);

                while (availableSl != 0)
                {
                    const size_t currentSl = FindFirstSetBit(availableSl);
                    size_t candidateAlignedUserAddr = 0;
                    size_t candidatePayloadNeeded = 0;
                    BlockHeader* best = FindSuitableInList(
                        _freeLists[currentFl][currentSl],
                        size,
                        alignment,
                        candidateAlignedUserAddr,
                        candidatePayloadNeeded);
                    if (best)
                    {
                        alignedUserAddr = candidateAlignedUserAddr;
                        payloadNeeded = candidatePayloadNeeded;
                        return best;
                    }

                    availableSl &= ~MakeBitmapMask(currentSl);
                }
            }

            return nullptr;
        }

        BlockHeader* FindSuitableInList(
            BlockHeader* head,
            size_t size,
            size_t alignment,
            size_t& alignedUserAddr,
            size_t& payloadNeeded) const
        {
            BlockHeader* best = nullptr;
            size_t bestAlignedUserAddr = 0;
            size_t bestPayloadNeeded = 0;

            for (BlockHeader* block = head; block != nullptr; block = block->GetNextFree())
            {
                size_t candidateAlignedUserAddr = 0;
                size_t candidatePayloadNeeded = 0;
                if (!TryGetAllocationLayout(
                        block,
                        size,
                        alignment,
                        candidateAlignedUserAddr,
                        candidatePayloadNeeded))
                {
                    continue;
                }

                if (!best || block->GetSize() < best->GetSize())
                {
                    best = block;
                    bestAlignedUserAddr = candidateAlignedUserAddr;
                    bestPayloadNeeded = candidatePayloadNeeded;
                }
            }

            alignedUserAddr = bestAlignedUserAddr;
            payloadNeeded = bestPayloadNeeded;
            return best;
        }

        void SplitBlock(BlockHeader* block, size_t usedSize)
        {
            const size_t blockSize = block->GetSize();
            if (usedSize > blockSize)
                return;

            const size_t remainingSize = blockSize - usedSize;
            if (remainingSize < sizeof(BlockHeader) + MIN_ALLOCATABLE_PAYLOAD)
                return;

            block->SetSize(usedSize);

            BlockHeader* newBlock = reinterpret_cast<BlockHeader*>(
                reinterpret_cast<uint8_t*>(block) + sizeof(BlockHeader) + usedSize);
            newBlock->Initialize(remainingSize - sizeof(BlockHeader), false, block);

            BlockHeader* nextBlock = GetNextPhysicalBlock(newBlock);
            if (IsValidBlock(nextBlock))
                nextBlock->SetPrevPhysical(newBlock);

            InsertIntoFreeList(newBlock);
        }

        BlockHeader* MergeWithNext(BlockHeader* block)
        {
            BlockHeader* nextBlock = GetNextPhysicalBlock(block);
            if (!IsValidBlock(nextBlock) || nextBlock->IsUsed())
                return block;

            RemoveFromFreeList(nextBlock);

            block->SetSize(block->GetSize() + sizeof(BlockHeader) + nextBlock->GetSize());

            BlockHeader* blockAfterNext = GetNextPhysicalBlock(nextBlock);
            if (IsValidBlock(blockAfterNext))
                blockAfterNext->SetPrevPhysical(block);

            nextBlock->ClearData();
            return block;
        }

        BlockHeader* MergeWithPrev(BlockHeader* block)
        {
            BlockHeader* prevBlock = block->GetPrevPhysical();
            if (!IsValidBlock(prevBlock) || prevBlock->IsUsed())
                return block;

            RemoveFromFreeList(prevBlock);

            prevBlock->SetSize(prevBlock->GetSize() + sizeof(BlockHeader) + block->GetSize());

            BlockHeader* nextBlock = GetNextPhysicalBlock(block);
            if (IsValidBlock(nextBlock))
                nextBlock->SetPrevPhysical(prevBlock);

            block->ClearData();
            return prevBlock;
        }

        void InsertIntoFreeList(BlockHeader* block)
        {
            size_t fl = 0;
            size_t sl = 0;
            MappingInsert(block->GetSize(), fl, sl);

            block->SetUsed(false);
            block->SetPrevFree(nullptr);
            block->SetNextFree(_freeLists[fl][sl]);

            if (_freeLists[fl][sl])
                _freeLists[fl][sl]->SetPrevFree(block);

            _freeLists[fl][sl] = block;
            _flBitmap |= MakeBitmapMask(fl);
            _slBitmaps[fl] |= MakeBitmapMask(sl);
        }

        void RemoveFromFreeList(BlockHeader* block)
        {
            size_t fl = 0;
            size_t sl = 0;
            MappingInsert(block->GetSize(), fl, sl);

            BlockHeader* prevFree = block->GetPrevFree();
            BlockHeader* nextFree = block->GetNextFree();

            if (prevFree)
                prevFree->SetNextFree(nextFree);
            else
                _freeLists[fl][sl] = nextFree;

            if (nextFree)
                nextFree->SetPrevFree(prevFree);

            if (!_freeLists[fl][sl])
            {
                _slBitmaps[fl] &= ~MakeBitmapMask(sl);
                if (_slBitmaps[fl] == 0)
                    _flBitmap &= ~MakeBitmapMask(fl);
            }

            block->ClearFreeLinks();
        }

        BlockHeader* GetNextPhysicalBlock(BlockHeader* block) const
        {
            size_t nextAddr = 0;
            if (!TryAdd(ToAddr(block), sizeof(BlockHeader), nextAddr) ||
                !TryAdd(nextAddr, block->GetSize(), nextAddr))
            {
                return nullptr;
            }

            return reinterpret_cast<BlockHeader*>(nextAddr);
        }

        static void StoreDistance(void* userPtr, size_t distance)
        {
            auto* distPtr = static_cast<uint8_t*>(PtrOffsetBytes(userPtr, -static_cast<std::ptrdiff_t>(DISTANCE_STORAGE_SIZE)));
            std::memcpy(distPtr, &distance, DISTANCE_STORAGE_SIZE);
        }

        static size_t ReadDistance(const void* userPtr)
        {
            size_t distance = 0;
            const auto* distPtr = static_cast<const uint8_t*>(
                PtrOffsetBytes(userPtr, -static_cast<std::ptrdiff_t>(DISTANCE_STORAGE_SIZE)));
            std::memcpy(&distance, distPtr, DISTANCE_STORAGE_SIZE);
            return distance;
        }

        static BlockHeader* GetHeaderFromUserPtr(void* userPtr)
        {
            const size_t distance = ReadDistance(userPtr);
            return reinterpret_cast<BlockHeader*>(
                static_cast<uint8_t*>(userPtr) - static_cast<std::ptrdiff_t>(distance));
        }

        BlockHeader* ValidateAndGetHeaderFromUserPtr(void* userPtr) const
        {
            const uintptr_t ptrAddr = ToAddr(static_cast<const uint8_t*>(userPtr));
            const uintptr_t dataBeginAddr = ToAddr(_pData);
            const uintptr_t dataEndAddr = dataBeginAddr + _size;

            if (ptrAddr <= dataBeginAddr || ptrAddr >= dataEndAddr)
            {
                MAF_DEALLOC_FAIL_VAL("TLSFAllocator pointer does not belong to this allocator", nullptr);
            }

            const size_t distance = ReadDistance(userPtr);
            const size_t minDistance = sizeof(BlockHeader) + DISTANCE_STORAGE_SIZE;
            if (distance < minDistance || distance > ptrAddr - dataBeginAddr)
            {
                MAF_DEALLOC_FAIL_VAL("TLSFAllocator pointer has invalid distance metadata", nullptr);
            }

            BlockHeader* block = reinterpret_cast<BlockHeader*>(
                static_cast<uint8_t*>(userPtr) - static_cast<std::ptrdiff_t>(distance));

            if (!IsValidBlock(block))
            {
                MAF_DEALLOC_FAIL_VAL("TLSFAllocator pointer does not map to a valid block header", nullptr);
            }

            if (!block->IsUsed())
            {
                MAF_DEALLOC_FAIL_VAL("TLSFAllocator pointer was already freed (double-free detected)", nullptr);
            }

            if (ToAddr(userPtr) - ToAddr(block) != distance)
            {
                MAF_DEALLOC_FAIL_VAL("TLSFAllocator distance metadata is inconsistent", nullptr);
            }

            return block;
        }

        bool IsValidBlock(const BlockHeader* block) const
        {
            if (!block)
                return false;

            const uintptr_t dataBeginAddr = ToAddr(_pData);
            const uintptr_t dataEndAddr = dataBeginAddr + _size;
            const uintptr_t blockStartAddr = ToAddr(block);
            size_t blockEndAddr = 0;
            if (!TryAdd(blockStartAddr, sizeof(BlockHeader), blockEndAddr))
                return false;

            return blockStartAddr >= dataBeginAddr && blockEndAddr <= dataEndAddr;
        }

        static size_t FindFirstSetBit(uint32_t value)
        {
            size_t bit = 0;
            while ((value & 1u) == 0u)
            {
                value >>= 1u;
                ++bit;
            }

            return bit;
        }

    private:
        static constexpr size_t HIGHEST_BIT_MASK = static_cast<size_t>(1) << (sizeof(size_t) * 8 - 1);
        static constexpr size_t MAX_BLOCK_SIZE = ~HIGHEST_BIT_MASK;
        static constexpr size_t DISTANCE_STORAGE_SIZE = sizeof(size_t);
        static constexpr size_t MIN_DEFAULT_ALIGNMENT = alignof(std::max_align_t);
        static constexpr size_t MIN_ALLOCATABLE_PAYLOAD = DISTANCE_STORAGE_SIZE + 1;
        static constexpr size_t BLOCK_GRANULARITY = alignof(BlockHeader);
        static constexpr size_t SMALL_BLOCK_STEP = 4;
        static constexpr size_t SMALL_BLOCK_SIZE = SL_COUNT * SMALL_BLOCK_STEP;

        static_assert(sizeof(BlockHeader) % BLOCK_GRANULARITY == 0,
            "BlockHeader size must preserve split alignment");

        static bool IsPowerOfTwo(size_t value)
        {
            return IsPowerOfTwoConstexpr(value);
        }

        static bool TryAdd(size_t lhs, size_t rhs, size_t& result)
        {
            if (lhs > std::numeric_limits<size_t>::max() - rhs)
                return false;

            result = lhs + rhs;
            return true;
        }

        static bool TryAlignUp(size_t value, size_t alignment, size_t& alignedValue)
        {
            size_t withPadding = 0;
            if (!TryAdd(value, alignment - 1, withPadding))
                return false;

            alignedValue = withPadding & ~(alignment - 1);
            return true;
        }

        template <typename T>
        static uintptr_t ToAddr(const T* p)
        {
            return reinterpret_cast<uintptr_t>(p);
        }

        static void* PtrOffsetBytes(void* ptr, std::ptrdiff_t offset)
        {
            return static_cast<void*>(static_cast<uint8_t*>(ptr) + offset);
        }

        static const void* PtrOffsetBytes(const void* ptr, std::ptrdiff_t offset)
        {
            return static_cast<const void*>(static_cast<const uint8_t*>(ptr) + offset);
        }

        static size_t Log2(size_t value)
        {
            size_t result = 0;
            while (value >>= 1u)
                ++result;
            return result;
        }

        static constexpr FlBitmap MakeBitmapMask(size_t index)
        {
            return static_cast<FlBitmap>(1u) << index;
        }

    private:
        uint8_t* _pData;
        size_t _size;
        size_t _defaultAlignment;
        BlockHeader* _pFirstBlock;

        FlBitmap _flBitmap;
        std::array<SlBitmap, FL_COUNT> _slBitmaps;
        FreeListArray _freeLists;
    };
}
