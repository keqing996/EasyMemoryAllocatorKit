#pragma once

#include <cstdint>
#include "Util/Util.hpp"

namespace EAllocKit
{
    /**
     * @brief Pool Allocator (also known as Fixed-Size Block Allocator)
     * 
     * @section strategy
     * - Pre-allocates a fixed number of equal-sized memory blocks
     * - Maintains a free list of available blocks
     * - Allocation/deallocation simply removes/adds blocks from/to the free list
     * - All blocks are the same size, determined at construction time
     * 
     * @section advantages
     * - Constant time O(1) allocation and deallocation
     * - Zero fragmentation: All blocks are the same size
     * - Predictable performance: Allocation time is deterministic
     * - Low overhead: Minimal metadata per block (just a next pointer)
     * - Cache friendly: Objects of same type allocated close together
     * - Excellent for same-sized object pools (particles, bullets, entities)
     * - No coalescing needed: Freed blocks directly reusable
     * 
     * @section disadvantages
     * - Fixed block size: Can only allocate objects of predetermined size
     * - Memory waste: Small objects waste space if block size is large
     * - Fixed capacity: Pool exhaustion if more objects needed than pre-allocated
     * - Upfront cost: All memory allocated at construction time
     * - No automatic expansion: Cannot grow pool dynamically
     * - Less flexible than general-purpose allocators
     * 
     * @section use_cases
     * - Game entity systems (same-sized game objects)
     * - Particle systems (thousands of identical particle objects)
     * - Object pools for networking (message objects, packet buffers)
     * - Event systems (event objects of known size)
     * - Memory pools for specific data structures (nodes in linked lists, trees)
     * - Real-time systems requiring predictable allocation times
     * - Frequently created/destroyed objects of the same type
     * 
     * @section not_recommended
     * - Objects with highly variable sizes
     * - Scenarios where object count cannot be predicted
     * - Applications with strict memory constraints (cannot pre-allocate)
     * - General-purpose memory allocation
     * - Mixed-size allocations
     * 
     * @note Current implementation is single-threaded; external synchronization required for multi-threading
     */
    template <size_t DefaultAlignment = 4>
    class PoolAllocator
    {
    public:
        struct Node
        {
            Node* pNext = nullptr;
        };

        explicit PoolAllocator(size_t blockSize, size_t blockNum);
        ~PoolAllocator();

        PoolAllocator(const PoolAllocator& rhs) = delete;
        PoolAllocator(PoolAllocator&& rhs) = delete;

    public:
        void* Allocate();
        void Deallocate(void* p);
        size_t GetAvailableBlockCount() const;
        Node* GetFreeListHeadNode() const;

    private:
        void* _pData;
        size_t _blockSize;
        size_t _blockNum;
        Node* _pFreeBlockList;
    };

    template<size_t DefaultAlignment>
    PoolAllocator<DefaultAlignment>::PoolAllocator(size_t blockSize, size_t blockNum)
        : _blockSize(blockSize)
        , _blockNum(blockNum)
    {
        // Ensure Node size is aligned to maintain user data alignment
        size_t alignedNodeSize = MemoryAllocatorUtil::UpAlignment(sizeof(Node), DefaultAlignment);
        size_t blockRequiredSize = alignedNodeSize + blockSize;
        // Align total block size to ensure next block starts at aligned address
        blockRequiredSize = MemoryAllocatorUtil::UpAlignment(blockRequiredSize, DefaultAlignment);
        size_t needSize = blockRequiredSize * blockNum;

        _pData = ::malloc(needSize);

        _pFreeBlockList = static_cast<Node*>(_pData);
        for (size_t i = 0; i < blockNum; i++)
        {
            Node* pBlockNode = static_cast<Node*>(MemoryAllocatorUtil::PtrOffsetBytes(_pData, i * blockRequiredSize));
            if (i == blockNum - 1)
                pBlockNode->pNext = nullptr;
            else
            {
                Node* pNextBlockNode = static_cast<Node*>(MemoryAllocatorUtil::PtrOffsetBytes(_pData, (i + 1) * blockRequiredSize));
                pBlockNode->pNext = pNextBlockNode;
            }
        }
    }

    template<size_t DefaultAlignment>
    PoolAllocator<DefaultAlignment>::~PoolAllocator()
    {
        ::free(_pData);
        _pData = nullptr;
    }

    template<size_t DefaultAlignment>
    void* PoolAllocator<DefaultAlignment>::Allocate()
    {
        if (_pFreeBlockList == nullptr)
            return nullptr;

        Node* pResult = _pFreeBlockList;
        _pFreeBlockList = _pFreeBlockList->pNext;
        
        // User data starts after aligned Node header
        size_t alignedNodeSize = MemoryAllocatorUtil::UpAlignment(sizeof(Node), DefaultAlignment);
        return MemoryAllocatorUtil::PtrOffsetBytes(pResult, alignedNodeSize);
    }

    template<size_t DefaultAlignment>
    void PoolAllocator<DefaultAlignment>::Deallocate(void* p)
    {
        // Node header is before user data, at aligned offset
        size_t alignedNodeSize = MemoryAllocatorUtil::UpAlignment(sizeof(Node), DefaultAlignment);
        Node* pNode = static_cast<Node*>(MemoryAllocatorUtil::PtrOffsetBytes(p, -static_cast<ptrdiff_t>(alignedNodeSize)));
        pNode->pNext = _pFreeBlockList;
        _pFreeBlockList = pNode;
    }

    template<size_t DefaultAlignment>
    size_t PoolAllocator<DefaultAlignment>::GetAvailableBlockCount() const
    {
        size_t count = 0;
        Node* pCurrent = _pFreeBlockList;
        while (pCurrent != nullptr)
        {
            pCurrent = pCurrent->pNext;
            count++;
        }

        return count;
    }

    template<size_t DefaultAlignment>
    typename PoolAllocator<DefaultAlignment>::Node* PoolAllocator<DefaultAlignment>::GetFreeListHeadNode() const
    {
        return _pFreeBlockList;
    }
}
