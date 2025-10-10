#pragma once

#include <new>
#include <cstdlib>
#include "Util/Util.hpp"
#include "Util/LinkNodeHeader.hpp"

namespace EAllocKit
{
    // Header for aligned allocations to store offset information
    struct AlignedAllocationHeader
    {
        static constexpr uint32_t MAGIC_NUMBER = 0xABCDEF00;
        uint32_t magic;           // Magic number to identify aligned allocations
        uint32_t offset;          // Offset from user pointer back to original header
        
        static bool IsAlignedAllocation(void* ptr)
        {
            AlignedAllocationHeader* header = GetHeaderFromUserPtr(ptr);
            return header->magic == MAGIC_NUMBER;
        }
        
        static AlignedAllocationHeader* GetHeaderFromUserPtr(void* ptr)
        {
            return reinterpret_cast<AlignedAllocationHeader*>(ptr) - 1;
        }
    };
    
    class FreeListAllocator
    {
    public:
        explicit FreeListAllocator(size_t size, size_t defaultAlignment = 4);
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
        void* AllocateAlignedWithPrefix(size_t size, size_t alignment);
        MemoryAllocatorLinkedNode* GetOriginalHeaderFromAlignedPtr(void* alignedPtr) const;

    private:
        void* _pData;
        size_t _size;
        size_t _defaultAlignment;
        MemoryAllocatorLinkedNode* _pFirstNode;
    };

    inline FreeListAllocator::FreeListAllocator(size_t size, size_t defaultAlignment)
        : _pData(nullptr)
        , _size(size)
        , _defaultAlignment(defaultAlignment)
        , _pFirstNode(nullptr)
    {
        size_t headerSize = MemoryAllocatorLinkedNode::PaddedSize(_defaultAlignment);
        if (_size < headerSize)
            _size = headerSize;

        // Use aligned_alloc if alignment > 16, otherwise use regular malloc
        if (_defaultAlignment <= 16) {
            _pData = ::malloc(_size);
        } else {
            // For larger alignments, use aligned_alloc
            // aligned_alloc requires size to be multiple of alignment
            size_t alignedSize = Util::UpAlignment(_size, _defaultAlignment);
            _pData = std::aligned_alloc(_defaultAlignment, alignedSize);
        }
        
        if (!_pData)
            throw std::bad_alloc();

        _pFirstNode = static_cast<MemoryAllocatorLinkedNode*>(_pData);
        _pFirstNode->SetUsed(false);
        _pFirstNode->SetSize(_size - headerSize);
        _pFirstNode->SetPrevNode(nullptr);
    }

    inline FreeListAllocator::~FreeListAllocator()
    {
        if (_pData) 
        {
            ::free(_pData);
            _pData = nullptr;
        }
    }

    inline void* FreeListAllocator::Allocate(size_t size)
    {
        return Allocate(size, _defaultAlignment);
    }

    inline void* FreeListAllocator::Allocate(size_t size, size_t alignment)
    {
        if (alignment <= _defaultAlignment) {
            // Fast path: use standard allocation for small alignments
            const size_t headerSize = MemoryAllocatorLinkedNode::PaddedSize(_defaultAlignment);
            const size_t requiredSize = Util::UpAlignment(size, _defaultAlignment);

            MemoryAllocatorLinkedNode* pCurrentNode = _pFirstNode;
            while (true)
            {
                if (pCurrentNode == nullptr)
                    return nullptr;

                if (!pCurrentNode->Used() && pCurrentNode->GetSize() >= requiredSize)
                {
                    pCurrentNode->SetUsed(true);

                    void* pResult = Util::PtrOffsetBytes(pCurrentNode, headerSize);

                    // Create a new node if left size is enough to place a new header.
                    size_t leftSize = pCurrentNode->GetSize() - requiredSize;
                    if (leftSize > headerSize)
                    {
                        pCurrentNode->SetSize(requiredSize);

                        MemoryAllocatorLinkedNode* pNextNode = pCurrentNode->MoveNext(_defaultAlignment);
                        pNextNode->SetPrevNode(pCurrentNode);
                        pNextNode->SetUsed(false);
                        pNextNode->SetSize(leftSize - headerSize);
                    }

                    return pResult;
                }

                // Move next
                pCurrentNode = pCurrentNode->MoveNext(_defaultAlignment);
                if (!IsValidHeader(pCurrentNode))
                    pCurrentNode = nullptr;
            }
        } else {
            // Slow path: use prefix offset storage for large alignments
            return AllocateAlignedWithPrefix(size, alignment);
        }
    }

    inline void FreeListAllocator::Deallocate(void* p)
    {
        MemoryAllocatorLinkedNode* pCurrentNode;
        
        // Check if this is an aligned allocation by looking for the magic number
        if (AlignedAllocationHeader::IsAlignedAllocation(p))
        {
            // This is a custom-aligned allocation, get the original header
            pCurrentNode = GetOriginalHeaderFromAlignedPtr(p);
        }
        else
        {
            // This is a standard allocation, use normal backtracking
            pCurrentNode = MemoryAllocatorLinkedNode::BackStepToLinkNode(p, _defaultAlignment);
        }
        
        pCurrentNode->SetUsed(false);

        // Merge forward
        while (true)
        {
            MemoryAllocatorLinkedNode* pNextNode = pCurrentNode->MoveNext(_defaultAlignment);
            if (!IsValidHeader(pNextNode) || pNextNode->Used())
                break;

            size_t oldSize = pCurrentNode->GetSize();
            size_t newSize = oldSize + MemoryAllocatorLinkedNode::PaddedSize(_defaultAlignment) + pNextNode->GetSize();
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
            const size_t oldSize = pPrevNode->GetSize();
            const size_t newSize = oldSize + MemoryAllocatorLinkedNode::PaddedSize(_defaultAlignment) + pCurrentNode->GetSize();
            pPrevNode->SetSize(newSize);

            // Adjust next node's prev
            MemoryAllocatorLinkedNode* pNextNode = pCurrentNode->MoveNext(_defaultAlignment);
            if (IsValidHeader(pNextNode))
                pNextNode->SetPrevNode(pPrevNode);

            // Clear this node
            pCurrentNode->ClearData();

            // Move backward
            pCurrentNode = pPrevNode;
        }
    }

    inline void* FreeListAllocator::GetMemoryBlockPtr() const
    {
        return _pData;
    }

    inline MemoryAllocatorLinkedNode* FreeListAllocator::GetFirstNode() const
    {
        return _pFirstNode;
    }

    inline void* FreeListAllocator::AllocateAlignedWithPrefix(size_t size, size_t alignment)
    {
        const size_t headerSize = MemoryAllocatorLinkedNode::PaddedSize(_defaultAlignment);
        const size_t alignedHeaderSize = sizeof(AlignedAllocationHeader);
        
        // Calculate the maximum possible space needed:
        // - Original header space
        // - Alignment padding (worst case: alignment - 1)
        // - Aligned header space  
        // - User data size
        size_t maxRequiredSize = headerSize + (alignment - 1) + alignedHeaderSize + size;
        
        // Find a suitable block using standard allocation logic
        MemoryAllocatorLinkedNode* pCurrentNode = _pFirstNode;
        while (true)
        {
            if (pCurrentNode == nullptr)
                return nullptr;

            if (!pCurrentNode->Used() && pCurrentNode->GetSize() >= maxRequiredSize)
            {
                pCurrentNode->SetUsed(true);

                // Calculate the aligned user data address
                uintptr_t baseAddr = reinterpret_cast<uintptr_t>(pCurrentNode) + headerSize;
                uintptr_t alignedAddr = baseAddr + alignedHeaderSize;
                alignedAddr = (alignedAddr + alignment - 1) & ~(alignment - 1);
                
                // Store the aligned allocation header
                AlignedAllocationHeader* alignedHeader = 
                    reinterpret_cast<AlignedAllocationHeader*>(alignedAddr - alignedHeaderSize);
                alignedHeader->magic = AlignedAllocationHeader::MAGIC_NUMBER;
                alignedHeader->offset = static_cast<uint32_t>(alignedAddr - reinterpret_cast<uintptr_t>(pCurrentNode));
                
                // Calculate actual used space
                size_t actualUsedSize = (alignedAddr - baseAddr) + size;
                
                // Create a new node if left size is enough to place a new header
                size_t leftSize = pCurrentNode->GetSize() - actualUsedSize;
                if (leftSize > headerSize)
                {
                    pCurrentNode->SetSize(actualUsedSize);

                    MemoryAllocatorLinkedNode* pNextNode = pCurrentNode->MoveNext(_defaultAlignment);
                    pNextNode->SetPrevNode(pCurrentNode);
                    pNextNode->SetUsed(false);
                    pNextNode->SetSize(leftSize - headerSize);
                }

                return reinterpret_cast<void*>(alignedAddr);
            }

            // Move next
            pCurrentNode = pCurrentNode->MoveNext(_defaultAlignment);
            if (!IsValidHeader(pCurrentNode))
                pCurrentNode = nullptr;
        }
    }

    inline MemoryAllocatorLinkedNode* FreeListAllocator::GetOriginalHeaderFromAlignedPtr(void* alignedPtr) const
    {
        AlignedAllocationHeader* alignedHeader = AlignedAllocationHeader::GetHeaderFromUserPtr(alignedPtr);
        uintptr_t originalAddr = reinterpret_cast<uintptr_t>(alignedPtr) - alignedHeader->offset;
        return reinterpret_cast<MemoryAllocatorLinkedNode*>(originalAddr);
    }

    inline bool FreeListAllocator::IsValidHeader(const MemoryAllocatorLinkedNode* pHeader) const
    {
        const size_t dataBeginAddr = Util::ToAddr(_pData);
        const size_t dataEndAddr = dataBeginAddr + _size;
        const size_t headerStartAddr = Util::ToAddr(pHeader);
        const size_t headerEndAddr = headerStartAddr + MemoryAllocatorLinkedNode::PaddedSize(_defaultAlignment);
        return headerStartAddr >= dataBeginAddr && headerEndAddr < dataEndAddr;
    }
}
