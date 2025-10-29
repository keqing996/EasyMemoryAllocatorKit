#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <stdexcept>

namespace EAllocKit
{
    class BuddyAllocator
    {
    private:
        static constexpr size_t MIN_BLOCK_SIZE = 32;  // Minimum allocation size
        static constexpr size_t MAX_ORDER = 32;       // Maximum number of size classes

        struct FreeBlock
        {
            FreeBlock* next;
        };
        
    public:
        explicit BuddyAllocator(size_t size, size_t defaultAlignment = 8);
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
        auto GetBuddy(void* block, size_t order) -> void*;
        auto GetBlockIndex(void* block) const -> size_t;
        auto GetBlockFromIndex(size_t index) const -> void*;
        auto IsBlockFree(size_t index, size_t order) const -> bool;
        auto MarkBlockUsed(size_t index, size_t order) -> void;
        auto MarkBlockFree(size_t index, size_t order) -> void;
        auto SplitBlock(size_t order) -> void;
        auto AllocateBlock(size_t order) -> void*;
        auto DeallocateBlock(void* ptr, size_t order) -> void;
        
    private: // Util functions
        static auto IsPowerOfTwo(size_t value) -> bool
        {
            return value > 0 && (value & (value - 1)) == 0;
        }

        static auto RoundUpToPowerOf2(size_t size) -> size_t
        {
            if (size == 0)
                return 1;

            size--;
            size |= size >> 1;
            size |= size >> 2;
            size |= size >> 4;
            size |= size >> 8;
            size |= size >> 16;

            if constexpr (sizeof(size_t) > 4)
                size |= size >> 32;

            size++;

            return size;
        }
        
        static auto Log2(size_t value) -> size_t
        {
            size_t result = 0;
            while (value >>= 1)
                result++;
            return result;
        }

    private:
        uint8_t* _pData;                   // Memory pool
        size_t _size;                      // Total size (power of 2)
        size_t _maxOrder;                  // Maximum order based on size
        size_t _defaultAlignment;          // Default alignment
        std::array<FreeBlock*, MAX_ORDER> _freeLists{};  // Free lists for each order
        uint8_t* _blockStatus;             // Bitmap for block allocation status
        size_t _bitmapSize;                // Size of bitmap in bytes
    };

    inline BuddyAllocator::BuddyAllocator(size_t size, size_t defaultAlignment)
        : _pData(nullptr)
        , _size(0)
        , _maxOrder(0)
        , _defaultAlignment(defaultAlignment)
        , _blockStatus(nullptr)
        , _bitmapSize(0)
    {
        if (!IsPowerOfTwo(defaultAlignment))
            throw std::invalid_argument("BuddyAllocator defaultAlignment must be a power of 2");
            
        // Round size down to power of 2
        _size = RoundUpToPowerOf2(size);
        if (_size < MIN_BLOCK_SIZE)
            _size = MIN_BLOCK_SIZE;
        
        // Calculate max order
        _maxOrder = Log2(_size / MIN_BLOCK_SIZE) + 1;
        if (_maxOrder > MAX_ORDER)
            _maxOrder = MAX_ORDER;
        
        // Initialize free lists
        _freeLists.fill(nullptr);
        
        // Calculate bitmap size (one bit per minimum-sized block)
        size_t numMinBlocks = _size / MIN_BLOCK_SIZE;
        _bitmapSize = (numMinBlocks + 7) / 8;
        
        // Allocate memory pool and bitmap
        size_t totalSize = _size + _bitmapSize;
        void* memory = ::malloc(totalSize);
        if (!memory)
            throw std::bad_alloc();
        
        _pData = static_cast<uint8_t*>(memory);
        _blockStatus = static_cast<uint8_t*>(memory) + _size;
        std::memset(_blockStatus, 0, _bitmapSize);
        
        // Add the entire memory pool as one free block at max order
        FreeBlock* initialBlock = reinterpret_cast<FreeBlock*>(_pData);
        initialBlock->next = nullptr;
        _freeLists[_maxOrder - 1] = initialBlock;
    }
    
    inline BuddyAllocator::~BuddyAllocator()
    {
        if (_pData)
            ::free(_pData);
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
        
        // Check for size overflow - if size is too large, return nullptr
        if (size > _size)
            return nullptr;

        alignment = std::max(alignment, _defaultAlignment);

        size_t blockSize = RoundUpToPowerOf2(size);
        if (blockSize == 0 || blockSize < size)
            return nullptr;

        if (blockSize < MIN_BLOCK_SIZE)
            blockSize = MIN_BLOCK_SIZE;

        if (blockSize < alignment)
            blockSize = RoundUpToPowerOf2(alignment);

        if (blockSize == 0 || blockSize < alignment)
            return nullptr;

        size_t order = GetOrderFromSize(blockSize);
        if (order >= _maxOrder)
            return nullptr;

        return AllocateBlock(order);
    }
    
    inline auto BuddyAllocator::Deallocate(void* ptr) -> void
    {
        if (!ptr)
            return;

        uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
        uintptr_t base = reinterpret_cast<uintptr_t>(_pData);

        if (addr < base || addr >= base + _size)
            return;

        size_t index = GetBlockIndex(ptr);

        for (size_t order = _maxOrder; order-- > 0;)
        {
            size_t blockSize = GetSizeFromOrder(order);
            if ((addr - base) % blockSize != 0)
                continue;

            size_t blocksPerAllocation = blockSize / MIN_BLOCK_SIZE;
            size_t blockIndex = index / blocksPerAllocation;
            size_t firstMinIndex = blockIndex * blocksPerAllocation;

            if (IsBlockFree(firstMinIndex, order))
                continue;

            DeallocateBlock(ptr, order);
            return;
        }
    }
    
    inline auto BuddyAllocator::GetOrderFromSize(size_t size) const -> size_t
    {
        return Log2(size / MIN_BLOCK_SIZE);
    }
    
    inline auto BuddyAllocator::GetSizeFromOrder(size_t order) const -> size_t
    {
        return MIN_BLOCK_SIZE << order;
    }
    
    inline auto BuddyAllocator::GetBuddy(void* block, size_t order) -> void*
    {
        uintptr_t addr = reinterpret_cast<uintptr_t>(block);
        uintptr_t base = reinterpret_cast<uintptr_t>(_pData);
        size_t blockSize = GetSizeFromOrder(order);
        
        // XOR with block size to get buddy address
        uintptr_t offset = addr - base;
        uintptr_t buddyOffset = offset ^ blockSize;
        
        return reinterpret_cast<void*>(base + buddyOffset);
    }
    
    inline auto BuddyAllocator::GetBlockIndex(void* block) const -> size_t
    {
        uintptr_t addr = reinterpret_cast<uintptr_t>(block);
        uintptr_t base = reinterpret_cast<uintptr_t>(_pData);
        return (addr - base) / MIN_BLOCK_SIZE;
    }
    
    inline auto BuddyAllocator::GetBlockFromIndex(size_t index) const -> void*
    {
        return static_cast<uint8_t*>(_pData) + (index * MIN_BLOCK_SIZE);
    }
    
    inline auto BuddyAllocator::IsBlockFree(size_t index, size_t order) const -> bool
    {
        size_t byteIndex = index / 8;
        size_t bitIndex = index % 8;
        
        if (byteIndex >= _bitmapSize)
            return false;
        
        return (_blockStatus[byteIndex] & (1 << bitIndex)) == 0;
    }
    
    inline auto BuddyAllocator::MarkBlockUsed(size_t index, size_t order) -> void
    {
        size_t blockSize = GetSizeFromOrder(order);
        size_t numMinBlocks = blockSize / MIN_BLOCK_SIZE;
        
        for (size_t i = 0; i < numMinBlocks; ++i)
        {
            size_t idx = index + i;
            size_t byteIndex = idx / 8;
            size_t bitIndex = idx % 8;
            
            if (byteIndex < _bitmapSize)
                _blockStatus[byteIndex] |= (1 << bitIndex);
        }
    }
    
    inline auto BuddyAllocator::MarkBlockFree(size_t index, size_t order) -> void
    {
        size_t blockSize = GetSizeFromOrder(order);
        size_t numMinBlocks = blockSize / MIN_BLOCK_SIZE;
        
        for (size_t i = 0; i < numMinBlocks; ++i)
        {
            size_t idx = index + i;
            size_t byteIndex = idx / 8;
            size_t bitIndex = idx % 8;
            
            if (byteIndex < _bitmapSize)
                _blockStatus[byteIndex] &= ~(1 << bitIndex);
        }
    }
    
    inline auto BuddyAllocator::SplitBlock(size_t order) -> void
    {
        if (order >= _maxOrder - 1)
            return;
        
        // Get a block from higher order
        if (!_freeLists[order + 1])
        {
            SplitBlock(order + 1);
            if (!_freeLists[order + 1])
                return;
        }
        
        // Remove block from higher order list
        FreeBlock* block = _freeLists[order + 1];
        _freeLists[order + 1] = block->next;
        
        // Split into two buddies
        size_t blockSize = GetSizeFromOrder(order);
        FreeBlock* buddy = reinterpret_cast<FreeBlock*>(
            reinterpret_cast<uint8_t*>(block) + blockSize
        );
        
        // Add both to current order list
        block->next = buddy;
        buddy->next = _freeLists[order];
        _freeLists[order] = block;
    }
    
    inline auto BuddyAllocator::AllocateBlock(size_t order) -> void*
    {
        if (order >= _maxOrder)
            return nullptr;
        
        // Find a free block of the requested order
        if (!_freeLists[order])
        {
            // Split a larger block
            SplitBlock(order);
            if (!_freeLists[order])
                return nullptr;
        }
        
        // Remove from free list
        FreeBlock* block = _freeLists[order];
        _freeLists[order] = block->next;
        
        // Mark as used
        size_t index = GetBlockIndex(block);
        MarkBlockUsed(index, order);
        
        return block;
    }
    
    inline auto BuddyAllocator::DeallocateBlock(void* ptr, size_t order) -> void
    {
        if (!ptr || order >= _maxOrder)
            return;
        
        size_t index = GetBlockIndex(ptr);
        
        // Try to merge with buddy
        while (order < _maxOrder - 1)
        {
            void* buddy = GetBuddy(ptr, order);
            size_t buddyIndex = GetBlockIndex(buddy);
            
            // Check if buddy is free
            bool buddyFree = true;
            size_t blockSize = GetSizeFromOrder(order);
            size_t numMinBlocks = blockSize / MIN_BLOCK_SIZE;
            
            for (size_t i = 0; i < numMinBlocks; ++i)
            {
                if (!IsBlockFree(buddyIndex + i, 0))
                {
                    buddyFree = false;
                    break;
                }
            }
            
            if (!buddyFree)
                break;
            
            // Remove buddy from free list
            FreeBlock** current = &_freeLists[order];
            while (*current)
            {
                if (*current == buddy)
                {
                    *current = (*current)->next;
                    break;
                }
                current = &((*current)->next);
            }
            
            // Merge with buddy (use lower address)
            if (buddy < ptr)
            {
                ptr = buddy;
                index = buddyIndex;
            }
            
            order++;
        }
        
        // Mark as free and add to free list
        MarkBlockFree(index, order);
        FreeBlock* block = static_cast<FreeBlock*>(ptr);
        block->next = _freeLists[order];
        _freeLists[order] = block;
    }
}
