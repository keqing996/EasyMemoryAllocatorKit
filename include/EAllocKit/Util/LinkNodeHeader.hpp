#pragma once

#include "Util.hpp"

namespace EAllocKit
{
    /**
     * @brief Linked Node Header for Memory Allocators
     * Memory layout:     
     * +------------------+------------------+
     * | Previous Pointer | Size + Used Flag |
     * +------------------+------------------+
     * | User Data Area...                   |
     * +-------------------------------------+
     */
    class MemoryAllocatorLinkedNode
    {
    public:
        size_t GetSize() const
        {
            return _usedAndSize & ~Util::HIGHEST_BIT_MASK;
        }

        void SetSize(size_t size)
        {
            _usedAndSize = (_usedAndSize & Util::HIGHEST_BIT_MASK) | (size & ~Util::HIGHEST_BIT_MASK);
        }

        bool Used() const
        {
            return (_usedAndSize & Util::HIGHEST_BIT_MASK) != 0;
        }

        void SetUsed(bool used)
        {
            if (used)
                _usedAndSize |= Util::HIGHEST_BIT_MASK;
            else
                _usedAndSize &= ~Util::HIGHEST_BIT_MASK;
        }

        MemoryAllocatorLinkedNode* GetPrevNode() const
        {
            return _pPrev;
        }

        void SetPrevNode(MemoryAllocatorLinkedNode* prev)
        {
            _pPrev = prev;
        }

        void ClearData()
        {
            _pPrev = nullptr;
            _usedAndSize = 0;
        }

        // Get pointer to the next physically adjacent block
        MemoryAllocatorLinkedNode* MoveNext(size_t alignment)
        {
            return Util::PtrOffsetBytes(this, GetSize() + PaddedSize(alignment));
        }

        // Get the aligned size of this header structure, his is the actual space the header occupies in memory
        // and the user data starts immediately after this header.
        static size_t PaddedSize(size_t alignment)
        {
            return Util::GetPaddedSize(sizeof(MemoryAllocatorLinkedNode), alignment);
        }

        // Recover the node header from a user data pointer
        static MemoryAllocatorLinkedNode* BackStepToLinkNode(void* ptr, size_t alignment)
        {
            return static_cast<MemoryAllocatorLinkedNode*>(Util::PtrOffsetBytes(ptr, -static_cast<ptrdiff_t>(PaddedSize(alignment))));
        }

    private:
        MemoryAllocatorLinkedNode* _pPrev;     ///< Pointer to previous block in physical memory
        size_t _usedAndSize;                    ///< Combined size (lower bits) and used flag (highest bit)
    };
}
