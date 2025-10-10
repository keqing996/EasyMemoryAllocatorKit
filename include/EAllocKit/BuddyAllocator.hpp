#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <stdexcept>
#include "EAllocKit/Util/Util.hpp"

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
        void* Allocate(size_t size);
        void* Allocate(size_t size, size_t alignment);
        void Deallocate(void* ptr);
        void* GetMemoryBlockPtr() const { return _pData; }
        size_t GetTotalSize() const { return _size; }
        
    private:
        size_t GetOrderFromSize(size_t size) const;
        size_t GetSizeFromOrder(size_t order) const;
        void* GetBuddy(void* block, size_t order);
        size_t GetBlockIndex(void* block) const;
        void* GetBlockFromIndex(size_t index) const;
        bool IsBlockFree(size_t index, size_t order) const;
        void MarkBlockUsed(size_t index, size_t order);
        void MarkBlockFree(size_t index, size_t order);
        void SplitBlock(size_t order);
        void* AllocateBlock(size_t order);
        void DeallocateBlock(void* ptr, size_t order);
        
    private:
        void* _pData;                      // Memory pool
        size_t _size;                      // Total size (power of 2)
        size_t _maxOrder;                  // Maximum order based on size
        size_t _defaultAlignment;          // Default alignment
        FreeBlock* _freeLists[MAX_ORDER]{};  // Free lists for each order
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
        if (!Util::IsPowerOfTwo(defaultAlignment))
            throw std::invalid_argument("BuddyAllocator defaultAlignment must be a power of 2");
            
        // Round size down to power of 2
        _size = Util::RoundUpToPowerOf2(size);
        if (_size < MIN_BLOCK_SIZE)
            _size = MIN_BLOCK_SIZE;
        
        // Calculate max order
        _maxOrder = Util::Log2(_size / MIN_BLOCK_SIZE) + 1;
        if (_maxOrder > MAX_ORDER)
            _maxOrder = MAX_ORDER;
        
        // Initialize free lists
        std::memset(_freeLists, 0, sizeof(_freeLists));
        
        // Calculate bitmap size (one bit per minimum-sized block)
        size_t numMinBlocks = _size / MIN_BLOCK_SIZE;
        _bitmapSize = (numMinBlocks + 7) / 8;
        
        // Allocate memory pool and bitmap
        size_t totalSize = _size + _bitmapSize;
        void* memory = ::malloc(totalSize);
        if (!memory)
            throw std::bad_alloc();
        
        _pData = memory;
        _blockStatus = static_cast<uint8_t*>(memory) + _size;
        std::memset(_blockStatus, 0, _bitmapSize);
        
        // Add the entire memory pool as one free block at max order
        FreeBlock* initialBlock = static_cast<FreeBlock*>(_pData);
        initialBlock->next = nullptr;
        _freeLists[_maxOrder - 1] = initialBlock;
    }
    
    inline BuddyAllocator::~BuddyAllocator()
    {
        if (_pData)
            ::free(_pData);
    }
    
    inline void* BuddyAllocator::Allocate(size_t size)
    {
        return Allocate(size, _defaultAlignment);
    }
    
    inline void* BuddyAllocator::Allocate(size_t size, size_t alignment)
    {
        if (size == 0)
            return nullptr;
            
        if (!Util::IsPowerOfTwo(alignment))
            throw std::invalid_argument("BuddyAllocator only supports power-of-2 alignments");
        
        // Buddy allocator doesn't support arbitrary alignment efficiently
        size_t adjustedSize = size;
        if (alignment > _defaultAlignment)
            adjustedSize = size + alignment;
        
        // Round up to power of 2, with minimum size
        size_t blockSize = Util::RoundUpToPowerOf2(adjustedSize);
        if (blockSize < MIN_BLOCK_SIZE)
            blockSize = MIN_BLOCK_SIZE;
        
        size_t order = GetOrderFromSize(blockSize);
        if (order >= _maxOrder)
            return nullptr;
        
        void* block = AllocateBlock(order);
        
        // Handle alignment if needed
        if (alignment > _defaultAlignment && block)
        {
            uintptr_t addr = reinterpret_cast<uintptr_t>(block);
            uintptr_t aligned = (addr + alignment - 1) & ~(alignment - 1);
            if (aligned != addr)
            {
                // For simplicity, allocate a larger block to accommodate alignment
                // Return the aligned address within the block
                return reinterpret_cast<void*>(aligned);
            }
        }
        
        return block;
    }
    
    inline void BuddyAllocator::Deallocate(void* ptr)
    {
        if (!ptr)
            return;
        
        // Find the order of this block by checking bitmap
        size_t index = GetBlockIndex(ptr);
        
        // Try to find the correct order by checking which order this block belongs to
        for (size_t order = 0; order < _maxOrder; ++order)
        {
            size_t blockSize = GetSizeFromOrder(order);
            size_t blockIndex = index / (blockSize / MIN_BLOCK_SIZE);
            
            if (IsBlockFree(blockIndex * (blockSize / MIN_BLOCK_SIZE), order))
                continue;
            
            // Check if this pointer aligns to this order
            uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
            uintptr_t base = reinterpret_cast<uintptr_t>(_pData);
            if ((addr - base) % blockSize == 0)
            {
                DeallocateBlock(ptr, order);
                return;
            }
        }
    }
    
    inline size_t BuddyAllocator::GetOrderFromSize(size_t size) const
    {
        return Util::Log2(size / MIN_BLOCK_SIZE);
    }
    
    inline size_t BuddyAllocator::GetSizeFromOrder(size_t order) const
    {
        return MIN_BLOCK_SIZE << order;
    }
    
    inline void* BuddyAllocator::GetBuddy(void* block, size_t order)
    {
        uintptr_t addr = reinterpret_cast<uintptr_t>(block);
        uintptr_t base = reinterpret_cast<uintptr_t>(_pData);
        size_t blockSize = GetSizeFromOrder(order);
        
        // XOR with block size to get buddy address
        uintptr_t offset = addr - base;
        uintptr_t buddyOffset = offset ^ blockSize;
        
        return reinterpret_cast<void*>(base + buddyOffset);
    }
    
    inline size_t BuddyAllocator::GetBlockIndex(void* block) const
    {
        uintptr_t addr = reinterpret_cast<uintptr_t>(block);
        uintptr_t base = reinterpret_cast<uintptr_t>(_pData);
        return (addr - base) / MIN_BLOCK_SIZE;
    }
    
    inline void* BuddyAllocator::GetBlockFromIndex(size_t index) const
    {
        return static_cast<uint8_t*>(_pData) + (index * MIN_BLOCK_SIZE);
    }
    
    inline bool BuddyAllocator::IsBlockFree(size_t index, size_t order) const
    {
        size_t byteIndex = index / 8;
        size_t bitIndex = index % 8;
        
        if (byteIndex >= _bitmapSize)
            return false;
        
        return (_blockStatus[byteIndex] & (1 << bitIndex)) == 0;
    }
    
    inline void BuddyAllocator::MarkBlockUsed(size_t index, size_t order)
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
    
    inline void BuddyAllocator::MarkBlockFree(size_t index, size_t order)
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
    
    inline void BuddyAllocator::SplitBlock(size_t order)
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
    
    inline void* BuddyAllocator::AllocateBlock(size_t order)
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
    
    inline void BuddyAllocator::DeallocateBlock(void* ptr, size_t order)
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
