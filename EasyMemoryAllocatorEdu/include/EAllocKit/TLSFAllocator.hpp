#pragma once

#include <cstddef>
#include <new>
#include <cstdlib>
#include <stdexcept>
#include <array>

namespace EAllocKit
{
    template<size_t FL_COUNT = 16, size_t SL_COUNT = 16>
    class TLSFAllocator
    {
    private:
        static constexpr bool IsPowerOfTwoConstexpr(size_t value)
        {
            return value > 0 && (value & (value - 1)) == 0;
        }
        
        static_assert(IsPowerOfTwoConstexpr(FL_COUNT) && FL_COUNT >= 4, "FL_COUNT must be a power of 2 and >= 4");
        static_assert(IsPowerOfTwoConstexpr(SL_COUNT) && SL_COUNT >= 4, "SL_COUNT must be a power of 2 and >= 4");
        
    private:
        /**
         * @brief Free Block Header for TLSF
         * Memory layout:     
         * +------------------+------------------+------------------+------------------+
         * | Previous Pointer | Size + Used Flag |   Next Free      |   Prev Free      |
         * +------------------+------------------+------------------+------------------+
         */
        class BlockHeader
        {
        public:
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
                _pPrevPhysical = nullptr;
                _usedAndSize = 0;
                _pNextFree = nullptr;
                _pPrevFree = nullptr;
            }

        private:
            BlockHeader* _pPrevPhysical;    ///< Pointer to previous block in physical memory
            size_t _usedAndSize;            ///< Combined size (lower bits) and used flag (highest bit)
            BlockHeader* _pNextFree;        ///< Next free block in the free list
            BlockHeader* _pPrevFree;        ///< Previous free block in the free list
        };

        using FlBitmap = uint32_t;
        using SlBitmap = uint32_t;
        using FreeListArray = std::array<std::array<BlockHeader*, SL_COUNT>, FL_COUNT>;

    public:
        explicit TLSFAllocator(size_t size, size_t defaultAlignment = sizeof(void*))
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
                
            size_t headerSize = sizeof(BlockHeader);
            size_t minSize = headerSize + 4 + _defaultAlignment;  // header + distance + minimal aligned data
            if (_size < minSize)
                _size = minSize;

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
                
            const size_t headerSize = sizeof(BlockHeader);
            
            // Calculate required space including alignment overhead
            size_t requiredSpace = CalculateRequiredSpace(size, alignment);
            
            // Find a suitable free block
            size_t fl, sl;
            MappingSearch(requiredSpace, fl, sl);
            BlockHeader* block = SearchSuitableBlock(fl, sl, requiredSpace);
            
            if (!block)
                return nullptr;
                
            // Remove block from free list
            RemoveFromFreeList(block);
            
            // Calculate aligned user data address
            size_t blockAddr = ToAddr(block);
            size_t afterHeaderAddr = blockAddr + headerSize;
            size_t minimalUserAddr = afterHeaderAddr + 4;  // Reserve 4 bytes for distance
            size_t alignedUserAddr = UpAlignment(minimalUserAddr, alignment);
            
            // Calculate actual used space
            size_t totalUsed = (alignedUserAddr - afterHeaderAddr) + size;
            
            // Split block if necessary
            SplitBlock(block, totalUsed);
            
            // Mark as used
            block->SetUsed(true);
            
            // Store distance for proper deallocation
            void* pAlignedUserData = reinterpret_cast<void*>(alignedUserAddr);
            uint32_t distance = static_cast<uint32_t>(alignedUserAddr - blockAddr);
            StoreDistance(pAlignedUserData, distance);
            
            return pAlignedUserData;
        }
        
        void Deallocate(void* p)
        {
            if (!p)
                return;
                
            // Use distance-based method to get back to the header
            BlockHeader* block = GetHeaderFromUserPtr(p);
            block->SetUsed(false);

            // Merge with adjacent free blocks
            block = MergeWithNext(block);
            block = MergeWithPrev(block);
            
            // Add back to free list
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

    private:
        void InitializeMemoryPool()
        {
            size_t headerSize = sizeof(BlockHeader);
            
            // Initialize the entire pool as one large free block
            _pFirstBlock = reinterpret_cast<BlockHeader*>(_pData);
            _pFirstBlock->SetUsed(false);
            _pFirstBlock->SetSize(_size - headerSize);
            _pFirstBlock->SetPrevPhysical(nullptr);
            _pFirstBlock->ClearFreeLinks();
            
            // Clear all free list arrays
            for (auto& slArray : _freeLists)
            {
                for (auto& blockPtr : slArray)
                {
                    blockPtr = nullptr;
                }
            }
            
            // Add the initial block to appropriate free list
            InsertIntoFreeList(_pFirstBlock);
        }
        
        size_t CalculateRequiredSpace(size_t size, size_t alignment) const
        {
            // We need space for alignment padding + actual size + distance storage
            // In worst case, we need (alignment - 1) extra bytes for alignment
            size_t totalSpace = size + alignment + 4;  // size + max_padding + distance storage
            
            // Ensure minimum allocation size
            const size_t minAlloc = 8;
            return totalSpace < minAlloc ? minAlloc : totalSpace;
        }
        
        void MappingInsert(size_t size, size_t& fl, size_t& sl) const
        {
            if (size < (1ULL << 6))  // Small sizes (less than 64 bytes)
            {
                fl = 0;
                sl = size >> 2;  // Divide by 4
                if (sl >= SL_COUNT) sl = SL_COUNT - 1;
            }
            else
            {
                fl = Log2(size);
                
                // Clamp fl to valid range
                if (fl >= FL_COUNT) 
                {
                    fl = FL_COUNT - 1;
                    sl = SL_COUNT - 1;
                }
                else
                {
                    // Extract second level index from remaining bits
                    size_t slLog = Log2(SL_COUNT);
                    if (fl >= slLog)
                    {
                        size_t slShift = fl - slLog;
                        sl = (size >> slShift) & (SL_COUNT - 1);
                    }
                    else
                    {
                        // For very small fl values, use linear mapping
                        sl = (size >> (fl > 0 ? fl - 1 : 0)) & (SL_COUNT - 1);
                    }
                }
            }
        }
        
        void MappingSearch(size_t size, size_t& fl, size_t& sl) const
        {
            MappingInsert(size, fl, sl);
        }
        
        BlockHeader* SearchSuitableBlock(size_t fl, size_t sl, size_t minSize)
        {
            // First, try the exact FL/SL category and higher SL in same FL
            SlBitmap slMap = _slBitmaps[fl] & (~0U << sl);
            if (slMap)
            {
                size_t foundSl = FindFirstSetBit(slMap);
                BlockHeader* block = _freeLists[fl][foundSl];
                if (block && block->GetSize() >= minSize)
                    return block;
            }
            
            // Search higher FL categories
            FlBitmap flMap = _flBitmap & (~0U << (fl + 1));
            if (flMap)
            {
                size_t foundFl = FindFirstSetBit(flMap);
                slMap = _slBitmaps[foundFl];
                if (slMap)
                {
                    size_t foundSl = FindFirstSetBit(slMap);
                    BlockHeader* block = _freeLists[foundFl][foundSl];
                    if (block && block->GetSize() >= minSize)
                        return block;
                }
            }
            
            return nullptr;
        }
        
        void SplitBlock(BlockHeader* block, size_t usedSize)
        {
            size_t headerSize = sizeof(BlockHeader);
            size_t blockSize = block->GetSize();
            size_t remainingSize = blockSize - usedSize;
            
            // Only split if remaining size is large enough for a new block
            if (remainingSize > headerSize + 4)
            {
                block->SetSize(usedSize);
                
                // Create new block from remaining space
                BlockHeader* newBlock = reinterpret_cast<BlockHeader*>(
                    reinterpret_cast<char*>(block) + headerSize + usedSize);
                
                newBlock->SetPrevPhysical(block);
                newBlock->SetUsed(false);
                newBlock->SetSize(remainingSize - headerSize);
                newBlock->ClearFreeLinks();
                
                // Update next block's previous pointer if it exists
                BlockHeader* nextBlock = GetNextPhysicalBlock(newBlock);
                if (IsValidBlock(nextBlock))
                {
                    nextBlock->SetPrevPhysical(newBlock);
                }
                
                // Add new block to free list
                InsertIntoFreeList(newBlock);
            }
        }
        
        BlockHeader* MergeWithNext(BlockHeader* block)
        {
            BlockHeader* nextBlock = GetNextPhysicalBlock(block);
            
            if (IsValidBlock(nextBlock) && !nextBlock->IsUsed())
            {
                // Remove next block from free list
                RemoveFromFreeList(nextBlock);
                
                // Merge blocks
                size_t headerSize = sizeof(BlockHeader);
                size_t newSize = block->GetSize() + headerSize + nextBlock->GetSize();
                block->SetSize(newSize);
                
                // Update the block after next block
                BlockHeader* blockAfterNext = GetNextPhysicalBlock(nextBlock);
                if (IsValidBlock(blockAfterNext))
                {
                    blockAfterNext->SetPrevPhysical(block);
                }
                
                nextBlock->ClearData();
            }
            
            return block;
        }
        
        BlockHeader* MergeWithPrev(BlockHeader* block)
        {
            BlockHeader* prevBlock = block->GetPrevPhysical();
            
            if (IsValidBlock(prevBlock) && !prevBlock->IsUsed())
            {
                // Remove prev block from free list
                RemoveFromFreeList(prevBlock);
                
                // Merge blocks
                size_t headerSize = sizeof(BlockHeader);
                size_t newSize = prevBlock->GetSize() + headerSize + block->GetSize();
                prevBlock->SetSize(newSize);
                
                // Update the block after current block
                BlockHeader* nextBlock = GetNextPhysicalBlock(block);
                if (IsValidBlock(nextBlock))
                {
                    nextBlock->SetPrevPhysical(prevBlock);
                }
                
                block->ClearData();
                return prevBlock;
            }
            
            return block;
        }
        
        void InsertIntoFreeList(BlockHeader* block)
        {
            size_t fl, sl;
            MappingInsert(block->GetSize(), fl, sl);
            
            block->SetNextFree(_freeLists[fl][sl]);
            block->SetPrevFree(nullptr);
            
            if (_freeLists[fl][sl])
            {
                _freeLists[fl][sl]->SetPrevFree(block);
            }
            
            _freeLists[fl][sl] = block;
            
            // Update bitmaps
            _flBitmap |= (1U << fl);
            _slBitmaps[fl] |= (1U << sl);
        }
        
        void RemoveFromFreeList(BlockHeader* block)
        {
            size_t fl, sl;
            MappingInsert(block->GetSize(), fl, sl);
            
            BlockHeader* prevFree = block->GetPrevFree();
            BlockHeader* nextFree = block->GetNextFree();
            
            if (prevFree)
            {
                prevFree->SetNextFree(nextFree);
            }
            else
            {
                _freeLists[fl][sl] = nextFree;
            }
            
            if (nextFree)
            {
                nextFree->SetPrevFree(prevFree);
            }
            
            // Update bitmaps if this was the last block in this category
            if (!_freeLists[fl][sl])
            {
                _slBitmaps[fl] &= ~(1U << sl);
                if (!_slBitmaps[fl])
                {
                    _flBitmap &= ~(1U << fl);
                }
            }
            
            block->ClearFreeLinks();
        }
        
        BlockHeader* GetNextPhysicalBlock(BlockHeader* block) const
        {
            size_t headerSize = sizeof(BlockHeader);
            char* nextAddr = reinterpret_cast<char*>(block) + headerSize + block->GetSize();
            return reinterpret_cast<BlockHeader*>(nextAddr);
        }
        
        static auto StoreDistance(void* userPtr, uint32_t distance)
        {
            uint32_t* distPtr = static_cast<uint32_t*>(PtrOffsetBytes(userPtr, -4));
            *distPtr = distance;
        }

        static auto ReadDistance(void* userPtr)
        {
            uint32_t* distPtr = static_cast<uint32_t*>(PtrOffsetBytes(userPtr, -4));
            return *distPtr;
        }

        static auto GetHeaderFromUserPtr(void* userPtr)
        {
            uint32_t distance = ReadDistance(userPtr);
            return static_cast<BlockHeader*>(PtrOffsetBytes(userPtr, -static_cast<std::ptrdiff_t>(distance)));
        }
        
        bool IsValidBlock(const BlockHeader* block) const
        {
            const size_t dataBeginAddr = ToAddr(_pData);
            const size_t dataEndAddr = dataBeginAddr + _size;
            const size_t blockStartAddr = ToAddr(block);
            const size_t blockEndAddr = blockStartAddr + sizeof(BlockHeader);
            return blockStartAddr >= dataBeginAddr && blockEndAddr < dataEndAddr;
        }
        
        static auto FindFirstSetBit(uint32_t value) -> size_t
        {
            if (value == 0) return 32;
            
            size_t bit = 0;
            if ((value & 0x0000FFFF) == 0) { bit += 16; value >>= 16; }
            if ((value & 0x000000FF) == 0) { bit += 8;  value >>= 8; }
            if ((value & 0x0000000F) == 0) { bit += 4;  value >>= 4; }
            if ((value & 0x00000003) == 0) { bit += 2;  value >>= 2; }
            if ((value & 0x00000001) == 0) { bit += 1; }
            
            return bit;
        }

    private: // Util functions and constants
        static constexpr size_t HIGHEST_BIT_MASK = static_cast<size_t>(1) << (sizeof(size_t) * 8 - 1);
        
        static auto IsPowerOfTwo(size_t value)
        {
            return value > 0 && (value & (value - 1)) == 0;
        }

        static auto UpAlignment(size_t size, size_t alignment)
        {
            return (size + alignment - 1) & ~(alignment - 1);
        }

        template <typename T>
        static auto ToAddr(const T* p)
        {
            return reinterpret_cast<size_t>(p);
        }

        template <typename T>
        static auto PtrOffsetBytes(T* ptr, std::ptrdiff_t offset)
        {
            return reinterpret_cast<T*>(static_cast<uint8_t*>(static_cast<void*>(ptr)) + offset);
        }
        
        static auto Log2(size_t value)
        {
            size_t result = 0;
            while (value >>= 1)
                result++;
            return result;
        }

    private:
        uint8_t* _pData;
        size_t _size;
        size_t _defaultAlignment;
        BlockHeader* _pFirstBlock;
        
        // TLSF data structures
        FlBitmap _flBitmap;                    ///< First level bitmap
        std::array<SlBitmap, FL_COUNT> _slBitmaps;  ///< Second level bitmaps
        FreeListArray _freeLists;              ///< Free block lists
    };
}