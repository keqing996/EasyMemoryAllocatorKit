#pragma once

#include <cstdint>
#include "Util/Util.hpp"
#include "Util/LinkNodeHeader.hpp"

namespace EAllocKit
{
    /**
     * @brief Stack Allocator (also known as LIFO Allocator or Double-Ended Stack Allocator)
     * 
     * @section strategy
     * - Allocates memory in a stack-like (LIFO - Last In First Out) manner
     * - Maintains a stack top pointer that moves up on allocation and down on deallocation
     * - Deallocations must occur in reverse order of allocations (like a stack)
     * - Uses linked headers to track allocation boundaries
     * 
     * @section advantages
     * - Fast allocation: O(1) time complexity, only pointer adjustment
     * - Fast deallocation: O(1) when following LIFO order
     * - Zero fragmentation: Stack-based allocation eliminates fragmentation
     * - Low overhead: Minimal metadata (stack pointer and block headers)
     * - Cache friendly: Sequential memory access improves cache performance
     * - Natural scoping: Matches nested function call patterns well
     * - Deterministic: Predictable performance for real-time systems
     * 
     * @section disadvantages
     * - LIFO constraint: Must deallocate in reverse order of allocation
     * - Inflexible: Cannot free middle allocations without freeing everything above
     * - Scope dependency: Requires strict discipline to maintain LIFO order
     * - Error prone: Breaking LIFO order leads to undefined behavior
     * - No random access deallocation: Limited flexibility compared to heap allocators
     * - Wasted space: Memory above the stack top cannot be used until deallocated
     * 
     * @section use_cases
     * - Function call stacks and local variable allocation
     * - Nested scope allocations (naturally LIFO)
     * - Temporary allocations during algorithm execution
     * - Recursive algorithms with predictable memory patterns
     * - Expression evaluation and parser temporary storage
     * - Render frame allocations (per-frame rendering data)
     * - Hierarchical scene graph traversal temporary data
     * 
     * @section not_recommended
     * - Random-order deallocation requirements
     * - Long-lived objects with unpredictable lifetimes
     * - Objects that need to outlive their allocation scope
     * - Scenarios where allocation order doesn't match deallocation order
     * - General-purpose heap replacement
     * 
     * @note Current implementation is single-threaded; external synchronization required for multi-threading
     * @warning Breaking the LIFO deallocation order will lead to memory corruption and undefined behavior
     */
    template <size_t DefaultAlignment = 4>
    class StackAllocator
    {
    public:
        explicit StackAllocator(size_t size);
        ~StackAllocator();

        StackAllocator(const StackAllocator& rhs) = delete;
        StackAllocator(StackAllocator&& rhs) = delete;

    public:
        void* Allocate(size_t size);
        void* Allocate(size_t size, size_t alignment);
        void Deallocate(void* p);
        MemoryAllocatorLinkedNode* GetStackTop() const;

    private:
        bool NewFrame(size_t requiredSize);

    private:
        void* _pData;
        size_t _size;
        MemoryAllocatorLinkedNode* _pStackTop;
    };

    template<size_t DefaultAlignment>
    StackAllocator<DefaultAlignment>::StackAllocator(size_t size)
        : _pData(nullptr)
        , _size(size)
        , _pStackTop(nullptr)
    {
        if (_size < MemoryAllocatorLinkedNode::PaddedSize<DefaultAlignment>())
            _size = MemoryAllocatorLinkedNode::PaddedSize<DefaultAlignment>();

        _pData = static_cast<uint8_t*>(::malloc(_size));
        _pStackTop = nullptr;
    }

    template<size_t DefaultAlignment>
    StackAllocator<DefaultAlignment>::~StackAllocator()
    {
        ::free(_pData);
        _pData = nullptr;
    }

    template<size_t DefaultAlignment>
    void* StackAllocator<DefaultAlignment>::Allocate(size_t size)
    {
        return Allocate(size, DefaultAlignment);
    }

    template<size_t DefaultAlignment>
    void* StackAllocator<DefaultAlignment>::Allocate(size_t size, size_t alignment)
    {
        size_t headerSize = MemoryAllocatorLinkedNode::PaddedSize<DefaultAlignment>();
        size_t requiredSize = MemoryAllocatorUtil::UpAlignment(size, alignment);

        if (!NewFrame(requiredSize))
            return nullptr;

        _pStackTop->SetUsed(true);
        return MemoryAllocatorUtil::PtrOffsetBytes(_pStackTop, headerSize);
    }

    template<size_t DefaultAlignment>
    bool StackAllocator<DefaultAlignment>::NewFrame(size_t requiredSize)
    {
        size_t headerSize = MemoryAllocatorLinkedNode::PaddedSize<DefaultAlignment>();
        size_t totalOccupySize = headerSize + requiredSize;

        void* pNextLinkNode = _pStackTop == nullptr
            ? _pData
            : _pStackTop->MoveNext<DefaultAlignment>();

        size_t availableSize = MemoryAllocatorUtil::ToAddr(_pData) + _size - MemoryAllocatorUtil::ToAddr(pNextLinkNode);
        if (availableSize < totalOccupySize)
            return false;

        MemoryAllocatorLinkedNode* pResult = static_cast<MemoryAllocatorLinkedNode*>(pNextLinkNode);
        pResult->SetSize(requiredSize);
        pResult->SetPrevNode(_pStackTop);
        _pStackTop = pResult;

        return true;
    }

    template<size_t DefaultAlignment>
    void StackAllocator<DefaultAlignment>::Deallocate(void* p)
    {
        MemoryAllocatorLinkedNode* pHeader = MemoryAllocatorLinkedNode::BackStepToLinkNode<DefaultAlignment>(p);
        pHeader->SetUsed(false);

        if (_pStackTop == pHeader)
        {
            while (true)
            {
                if (_pStackTop == nullptr || _pStackTop->Used())
                    break;

                MemoryAllocatorLinkedNode* pPrevNode = _pStackTop->GetPrevNode();
                _pStackTop->ClearData();
                _pStackTop = pPrevNode;
            }
        }
    }

    template<size_t DefaultAlignment>
    MemoryAllocatorLinkedNode* StackAllocator<DefaultAlignment>::GetStackTop() const
    {
        return _pStackTop;
    }
}

