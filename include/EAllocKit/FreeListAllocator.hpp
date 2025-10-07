#pragma once

#include <cstdint>
#include "Util/Util.hpp"
#include "Util/LinkNodeHeader.hpp"

namespace EAllocKit
{
    /**
     * @brief Free List Allocator
     * 
     * @section strategy Allocation Strategy
     * - Maintains a linked list of free memory blocks
     * - Uses First Fit strategy to find suitable free blocks
     * - Supports arbitrary size and alignment requirements
     * - Returns blocks to the free list on deallocation and attempts to merge adjacent blocks
     * 
     * @section advantages
     * - High flexibility: Supports arbitrary-sized allocations and deallocations
     * - Memory reuse: Individual blocks can be freed independently for reuse
     * - Reduces system calls: Pre-allocates large chunks to minimize malloc/free overhead
     * - Alignment support: Can specify memory alignment requirements
     * 
     * @section disadvantages
     * - Memory fragmentation: Long-term use leads to external fragmentation, reducing utilization
     * - Performance overhead: Allocation/deallocation requires list traversal with O(n) complexity
     * - Metadata overhead: Each block requires header information for size and linking
     * - Cache unfriendly: Scattered memory blocks may result in poor cache locality
     * - Non-deterministic: Allocation time depends on list length, unsuitable for real-time systems
     * 
     * @section use_cases
     * - Scenarios with varying object lifetimes (need to free objects independently)
     * - Allocations with widely varying sizes
     * - Long-running applications requiring memory reuse
     * - Foundation for general-purpose memory managers
     * - Game resource managers (textures, audio, etc.)
     * 
     * @section not_recommended
     * - Real-time systems requiring deterministic allocation times
     * - High-frequency small object allocations (consider PoolAllocator instead)
     * - Performance-critical hot paths
     * - LIFO memory usage patterns (use StackAllocator instead)
     * 
     * @note Current implementation is single-threaded; external synchronization required for multi-threading
     */
    template <size_t DefaultAlignment = 4>
    class FreeListAllocator
    {
    public:
        explicit FreeListAllocator(size_t size);
        ~FreeListAllocator();

        FreeListAllocator(const FreeListAllocator& rhs) = delete;
        FreeListAllocator(FreeListAllocator&& rhs) = delete;

    public:
        void* Allocate(size_t size);
        void* Allocate(size_t size, size_t alignment);
        void Deallocate(void* p);
        void* GetMemoryBlockPtr() const;
        MemoryAllocatorLinkedNode* GetFirstNode() const;

    private:
        bool IsValidHeader(const MemoryAllocatorLinkedNode* pHeader) const;

    private:
        void* _pData;
        size_t _size;
        MemoryAllocatorLinkedNode* _pFirstNode;
    };

    template<size_t DefaultAlignment>
    FreeListAllocator<DefaultAlignment>::FreeListAllocator(size_t size)
        : _pData(nullptr)
        , _size(size)
        , _pFirstNode(nullptr)
    {
        if (_size < MemoryAllocatorLinkedNode::PaddedSize<DefaultAlignment>())
            _size = MemoryAllocatorLinkedNode::PaddedSize<DefaultAlignment>();

        _pData = ::malloc(_size);

        _pFirstNode = static_cast<MemoryAllocatorLinkedNode*>(_pData);
        _pFirstNode->SetUsed(false);
        _pFirstNode->SetSize(_size - MemoryAllocatorLinkedNode::PaddedSize<DefaultAlignment>());
        _pFirstNode->SetPrevNode(nullptr);
    }

    template<size_t DefaultAlignment>
    FreeListAllocator<DefaultAlignment>::~FreeListAllocator()
    {
        ::free(_pData);
        _pData = nullptr;
    }

    template<size_t DefaultAlignment>
    void* FreeListAllocator<DefaultAlignment>::Allocate(size_t size)
    {
        return Allocate(size, DefaultAlignment);
    }

    template<size_t DefaultAlignment>
    void* FreeListAllocator<DefaultAlignment>::Allocate(size_t size, size_t alignment)
    {
        size_t headerSize = MemoryAllocatorLinkedNode::PaddedSize<DefaultAlignment>();
        size_t requiredSize = MemoryAllocatorUtil::UpAlignment(size, alignment);

        MemoryAllocatorLinkedNode* pCurrentNode = _pFirstNode;
        while (true)
        {
            if (pCurrentNode == nullptr)
                return nullptr;

            if (!pCurrentNode->Used() && pCurrentNode->GetSize() >= requiredSize)
            {
                pCurrentNode->SetUsed(true);

                void* pResult = MemoryAllocatorUtil::PtrOffsetBytes(pCurrentNode, headerSize);

                // Create a new node if left size is enough to place a new header.
                size_t leftSize = pCurrentNode->GetSize() - requiredSize;
                if (leftSize > headerSize)
                {
                    pCurrentNode->SetSize(requiredSize);

                    MemoryAllocatorLinkedNode* pNextNode = pCurrentNode->MoveNext<DefaultAlignment>();
                    pNextNode->SetPrevNode(pCurrentNode);
                    pNextNode->SetUsed(false);
                    pNextNode->SetSize(leftSize - headerSize);
                }

                return pResult;
            }

            // Move next
            pCurrentNode = pCurrentNode->MoveNext<DefaultAlignment>();
            if (!IsValidHeader(pCurrentNode))
                pCurrentNode = nullptr;
        }
    }

    template<size_t DefaultAlignment>
    void FreeListAllocator<DefaultAlignment>::Deallocate(void* p)
    {
        MemoryAllocatorLinkedNode* pCurrentNode = MemoryAllocatorLinkedNode::BackStepToLinkNode<DefaultAlignment>(p);
        pCurrentNode->SetUsed(false);

        // Merge forward
        while (true)
        {
            MemoryAllocatorLinkedNode* pNextNode = pCurrentNode->MoveNext<DefaultAlignment>();
            if (!IsValidHeader(pNextNode) || pNextNode->Used())
                break;

            size_t oldSize = pCurrentNode->GetSize();
            size_t newSize = oldSize + MemoryAllocatorLinkedNode::PaddedSize<DefaultAlignment>() + pNextNode->GetSize();
            pNextNode->ClearData();
            pCurrentNode->SetSize(newSize);
        }

        // Merge backward
        while (true)
        {
            MemoryAllocatorLinkedNode* pPrevNode = pCurrentNode->GetPrevNode();
            if (!IsValidHeader(pPrevNode) || pPrevNode->Used())
                break;

            // Adjust prev node's size
            size_t oldSize = pPrevNode->GetSize();
            size_t newSize = oldSize + MemoryAllocatorLinkedNode::PaddedSize<DefaultAlignment>() + pCurrentNode->GetSize();
            pPrevNode->SetSize(newSize);

            // Adjust next node's prev
            MemoryAllocatorLinkedNode* pNextNode = pCurrentNode->MoveNext<DefaultAlignment>();
            if (IsValidHeader(pNextNode))
                pNextNode->SetPrevNode(pPrevNode);

            // Clear this node
            pCurrentNode->ClearData();

            // Move backward
            pCurrentNode = pPrevNode;
        }
    }

    template<size_t DefaultAlignment>
    void* FreeListAllocator<DefaultAlignment>::GetMemoryBlockPtr() const
    {
        return _pData;
    }

    template<size_t DefaultAlignment>
    MemoryAllocatorLinkedNode* FreeListAllocator<DefaultAlignment>::GetFirstNode() const
    {
        return _pFirstNode;
    }

    template<size_t DefaultAlignment>
    bool FreeListAllocator<DefaultAlignment>::IsValidHeader(const MemoryAllocatorLinkedNode* pHeader) const
    {
        size_t dataBeginAddr = MemoryAllocatorUtil::ToAddr(_pData);
        size_t dataEndAddr = dataBeginAddr + _size;
        size_t headerStartAddr = MemoryAllocatorUtil::ToAddr(pHeader);
        size_t headerEndAddr = headerStartAddr + MemoryAllocatorLinkedNode::PaddedSize<DefaultAlignment>();
        return headerStartAddr >= dataBeginAddr && headerEndAddr < dataEndAddr;
    }
}
