#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>

#if !defined(__cpp_aligned_new) || (__cpp_aligned_new < 201606L)
#error "BuddyAllocator requires C++17 aligned operator new/delete support"
#endif

namespace EAllocKit
{
    class BuddyAllocator
    {
    private:
        static constexpr size_t MIN_BLOCK_SIZE = 32;
        static constexpr size_t MAX_ORDER = 32;

        struct FreeBlock
        {
            FreeBlock* next;
        };

        static_assert(MAX_ORDER <= std::numeric_limits<uint8_t>::max(),
                      "BuddyAllocator metadata order storage must fit in uint8_t");
        static_assert(MIN_BLOCK_SIZE >= sizeof(FreeBlock),
                      "BuddyAllocator minimum block size must hold a freelist node");
        static_assert((MIN_BLOCK_SIZE % alignof(FreeBlock)) == 0,
                      "BuddyAllocator minimum block size must preserve freelist alignment");

        enum class BlockState : uint8_t
        {
            Interior,
            FreeHead,
            UsedHead,
        };

        struct BlockMeta
        {
            uint8_t order = 0;
            BlockState state = BlockState::Interior;
        };

    public:
        explicit BuddyAllocator(size_t size, size_t defaultAlignment = alignof(std::max_align_t));
        ~BuddyAllocator();

        BuddyAllocator(const BuddyAllocator& rhs) = delete;
        BuddyAllocator(BuddyAllocator&& rhs) = delete;

    public:
        auto Allocate(size_t size) -> void*;
        auto Allocate(size_t size, size_t alignment) -> void*;
        auto Deallocate(void* ptr) -> void;
        auto GetMemoryBlockPtr() const -> void* { return _pData; }
        auto GetTotalSize() const -> size_t { return _size; }

    private:
        auto GetOrderFromSize(size_t size) const -> size_t;
        auto GetSizeFromOrder(size_t order) const -> size_t;
        auto GetBlockIndex(const void* block) const -> size_t;
        auto GetBlockFromIndex(size_t index) const -> void*;
        auto GetBuddyIndex(size_t index, size_t order) const -> size_t;
        static auto ConstructFreeBlock(void* storage, FreeBlock* next) -> FreeBlock*;
        static auto DestroyFreeBlock(FreeBlock* block) -> void;
        auto RemoveFreeBlock(size_t order, void* ptr) -> bool;
        auto PushFreeBlock(size_t order, void* ptr) -> void;
        auto SetBlockState(size_t index, size_t order, BlockState headState) -> void;
        auto SplitToOrder(size_t order) -> bool;
        auto AllocateBlock(size_t order) -> void*;
        auto DeallocateBlock(size_t index, size_t order) -> void;

    private:
        static constexpr auto IsPowerOfTwo(size_t value) -> bool
        {
            return value > 0 && (value & (value - 1)) == 0;
        }

        static constexpr auto MinimumSafeAlignment() -> size_t
        {
            size_t alignment = alignof(std::max_align_t);
            size_t rounded = 1;

            while (rounded < alignment)
                rounded <<= 1;

            return rounded;
        }

        static auto RoundUpToPowerOf2(size_t value, size_t& rounded) -> bool
        {
            if (value <= 1)
            {
                rounded = 1;
                return true;
            }

            value--;

            for (size_t shift = 1; shift < sizeof(size_t) * 8; shift <<= 1)
                value |= value >> shift;

            if (value == std::numeric_limits<size_t>::max())
                return false;

            rounded = value + 1;
            return true;
        }

    private:
        uint8_t* _pData;
        size_t _size;
        size_t _maxOrder;
        size_t _defaultAlignment;
        size_t _arenaAlignment;
        size_t _minBlockCount;
        std::array<FreeBlock*, MAX_ORDER> _freeLists{};
        std::unique_ptr<BlockMeta[]> _blockMeta;
    };

    inline BuddyAllocator::BuddyAllocator(size_t size, size_t defaultAlignment)
        : _pData(nullptr)
        , _size(0)
        , _maxOrder(0)
        , _defaultAlignment(0)
        , _arenaAlignment(0)
        , _minBlockCount(0)
    {
        if (!IsPowerOfTwo(defaultAlignment))
            throw std::invalid_argument("BuddyAllocator defaultAlignment must be a power of 2");

        const size_t minimumAlignment = MinimumSafeAlignment();
        _defaultAlignment = std::max(defaultAlignment, minimumAlignment);

        size_t roundedSize = 0;
        if (!RoundUpToPowerOf2(size, roundedSize))
            throw std::length_error("BuddyAllocator size exceeds supported range");

        _size = std::max(roundedSize, MIN_BLOCK_SIZE);
        _size = std::max(_size, _defaultAlignment);

        if (_size / MIN_BLOCK_SIZE > std::numeric_limits<size_t>::max() / sizeof(BlockMeta))
            throw std::length_error("BuddyAllocator size exceeds supported range");

        size_t blockSize = MIN_BLOCK_SIZE;
        _maxOrder = 1;

        while (blockSize < _size)
        {
            if (_maxOrder >= MAX_ORDER || blockSize > (std::numeric_limits<size_t>::max() / 2))
                throw std::length_error("BuddyAllocator size exceeds supported range");

            blockSize <<= 1;
            ++_maxOrder;
        }

        _arenaAlignment = _size;
        _minBlockCount = _size / MIN_BLOCK_SIZE;
        _freeLists.fill(nullptr);

        // Buddy blocks are always power-of-two sized/aligned, so the arena must
        // itself be aligned to the rounded arena size. This relies on the
        // implementation honoring C++17 aligned operator new/delete requests.
        void* arena = ::operator new(_size, std::align_val_t(_arenaAlignment));

        try
        {
            _blockMeta = std::make_unique<BlockMeta[]>(_minBlockCount);
        }
        catch (...)
        {
            ::operator delete(arena, std::align_val_t(_arenaAlignment));
            throw;
        }

        _pData = static_cast<uint8_t*>(arena);
        std::fill_n(_blockMeta.get(), _minBlockCount, BlockMeta{});
        SetBlockState(0, _maxOrder - 1, BlockState::FreeHead);
        auto* initialBlock = ConstructFreeBlock(_pData, nullptr);
        _freeLists[_maxOrder - 1] = initialBlock;
    }

    inline BuddyAllocator::~BuddyAllocator()
    {
        if (_pData)
            ::operator delete(_pData, std::align_val_t(_arenaAlignment));
    }

    inline auto BuddyAllocator::Allocate(size_t size) -> void*
    {
        return Allocate(size, _defaultAlignment);
    }

    inline auto BuddyAllocator::Allocate(size_t size, size_t alignment) -> void*
    {
        if (size == 0)
            return nullptr;

        if (!IsPowerOfTwo(alignment))
            throw std::invalid_argument("BuddyAllocator only supports power-of-2 alignments");

        alignment = std::max(alignment, _defaultAlignment);
        if (alignment > _size)
            return nullptr;

        size_t roundedSize = 0;
        if (!RoundUpToPowerOf2(size, roundedSize))
            return nullptr;

        size_t blockSize = std::max(roundedSize, MIN_BLOCK_SIZE);
        blockSize = std::max(blockSize, alignment);

        if (blockSize > _size)
            return nullptr;

        return AllocateBlock(GetOrderFromSize(blockSize));
    }

    inline auto BuddyAllocator::Deallocate(void* ptr) -> void
    {
        if (!ptr || !_pData)
            return;

        const uintptr_t address = reinterpret_cast<uintptr_t>(ptr);
        const uintptr_t base = reinterpret_cast<uintptr_t>(_pData);
        if (address < base)
            return;

        const size_t offset = static_cast<size_t>(address - base);
        if (offset >= _size || (offset % MIN_BLOCK_SIZE) != 0)
            return;

        const size_t index = offset / MIN_BLOCK_SIZE;
        if (_blockMeta[index].state != BlockState::UsedHead)
            return;

        DeallocateBlock(index, _blockMeta[index].order);
    }

    inline auto BuddyAllocator::GetOrderFromSize(size_t size) const -> size_t
    {
        size_t order = 0;
        size_t blockSize = MIN_BLOCK_SIZE;

        while (blockSize < size)
        {
            blockSize <<= 1;
            ++order;
        }

        return order;
    }

    inline auto BuddyAllocator::GetSizeFromOrder(size_t order) const -> size_t
    {
        return MIN_BLOCK_SIZE << order;
    }

    inline auto BuddyAllocator::GetBlockIndex(const void* block) const -> size_t
    {
        const uintptr_t address = reinterpret_cast<uintptr_t>(block);
        const uintptr_t base = reinterpret_cast<uintptr_t>(_pData);
        return static_cast<size_t>((address - base) / MIN_BLOCK_SIZE);
    }

    inline auto BuddyAllocator::GetBlockFromIndex(size_t index) const -> void*
    {
        return _pData + (index * MIN_BLOCK_SIZE);
    }

    inline auto BuddyAllocator::GetBuddyIndex(size_t index, size_t order) const -> size_t
    {
        return index ^ (size_t(1) << order);
    }

    inline auto BuddyAllocator::ConstructFreeBlock(void* storage, FreeBlock* next) -> FreeBlock*
    {
        return std::launder(::new (storage) FreeBlock{next});
    }

    inline auto BuddyAllocator::DestroyFreeBlock(FreeBlock* block) -> void
    {
        std::destroy_at(block);
    }

    inline auto BuddyAllocator::RemoveFreeBlock(size_t order, void* ptr) -> bool
    {
        FreeBlock** current = &_freeLists[order];

        while (*current)
        {
            FreeBlock* block = *current;
            if (block == ptr)
            {
                *current = block->next;
                DestroyFreeBlock(block);
                return true;
            }

            current = &(block->next);
        }

        return false;
    }

    inline auto BuddyAllocator::PushFreeBlock(size_t order, void* ptr) -> void
    {
        FreeBlock* block = ConstructFreeBlock(ptr, _freeLists[order]);
        _freeLists[order] = block;
    }

    inline auto BuddyAllocator::SetBlockState(size_t index, size_t order, BlockState headState) -> void
    {
        const size_t span = size_t(1) << order;

        for (size_t i = 0; i < span; ++i)
        {
            _blockMeta[index + i].order = static_cast<uint8_t>(order);
            _blockMeta[index + i].state = (i == 0) ? headState : BlockState::Interior;
        }
    }

    inline auto BuddyAllocator::SplitToOrder(size_t order) -> bool
    {
        size_t splitOrder = order;
        while (splitOrder < _maxOrder && !_freeLists[splitOrder])
            ++splitOrder;

        if (splitOrder >= _maxOrder)
            return false;

        FreeBlock* block = _freeLists[splitOrder];
        _freeLists[splitOrder] = block->next;
        DestroyFreeBlock(block);

        size_t blockIndex = GetBlockIndex(block);
        while (splitOrder > order)
        {
            --splitOrder;

            const size_t buddyIndex = blockIndex + (size_t(1) << splitOrder);
            SetBlockState(blockIndex, splitOrder, BlockState::FreeHead);
            SetBlockState(buddyIndex, splitOrder, BlockState::FreeHead);
            PushFreeBlock(splitOrder, GetBlockFromIndex(buddyIndex));
        }

        PushFreeBlock(order, block);
        return true;
    }

    inline auto BuddyAllocator::AllocateBlock(size_t order) -> void*
    {
        if (order >= _maxOrder)
            return nullptr;

        if (!_freeLists[order] && !SplitToOrder(order))
            return nullptr;

        FreeBlock* block = _freeLists[order];
        _freeLists[order] = block->next;
        DestroyFreeBlock(block);

        const size_t index = GetBlockIndex(block);
        SetBlockState(index, order, BlockState::UsedHead);
        return block;
    }

    inline auto BuddyAllocator::DeallocateBlock(size_t index, size_t order) -> void
    {
        while (order + 1 < _maxOrder)
        {
            const size_t buddyIndex = GetBuddyIndex(index, order);
            if (_blockMeta[buddyIndex].state != BlockState::FreeHead || _blockMeta[buddyIndex].order != order)
                break;

            if (!RemoveFreeBlock(order, GetBlockFromIndex(buddyIndex)))
                break;

            index = std::min(index, buddyIndex);
            ++order;
        }

        SetBlockState(index, order, BlockState::FreeHead);
        PushFreeBlock(order, GetBlockFromIndex(index));
    }
}
