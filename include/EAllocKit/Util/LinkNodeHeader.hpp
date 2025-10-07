#pragma once

#include "Util.hpp"

namespace EAllocKit
{
    /**
     * @brief Linked Node Header for Memory Allocators
     * 
     * @section purpose Purpose
     * Represents a memory block header used by linked-list based memory allocators
     * (FreeListAllocator and StackAllocator). Each allocated or free block has this
     * header prepended to track block metadata.
     * 
     * @section memory_layout Memory Layout
     * ```
     * +------------------+------------------+
     * | Previous Pointer | Size + Used Flag |
     * +------------------+------------------+
     * | User Data Area...                   |
     * +-------------------------------------+
     * ```
     * 
     * @section size_encoding Size Encoding
     * Uses the highest bit of `_usedAndSize` to store the "used" flag:
     * - Bit 63 (or 31 on 32-bit): Used flag (1 = allocated, 0 = free)
     * - Bits 0-62 (or 0-30): Actual block size in bytes
     *
     */
    class MemoryAllocatorLinkedNode
    {
    public:
        /**
         * @brief Get the size of the memory block (excluding header)
         * @return Size in bytes of the usable data area
         * @note The size is stored in the lower bits, with the highest bit reserved for the used flag
         */
        size_t GetSize() const
        {
            return _usedAndSize & ~MemoryAllocatorUtil::HIGHEST_BIT_MASK;
        }

        /**
         * @brief Set the size of the memory block (excluding header)
         * @param size Size in bytes of the usable data area
         * @note Preserves the used flag in the highest bit
         */
        void SetSize(size_t size)
        {
            _usedAndSize = (_usedAndSize & MemoryAllocatorUtil::HIGHEST_BIT_MASK) | (size & ~MemoryAllocatorUtil::HIGHEST_BIT_MASK);
        }

        /**
         * @brief Check if the block is currently in use (allocated)
         * @return true if the block is allocated, false if free
         * @note Uses the highest bit of _usedAndSize for efficient storage
         */
        bool Used() const
        {
            return (_usedAndSize & MemoryAllocatorUtil::HIGHEST_BIT_MASK) != 0;
        }

        /**
         * @brief Mark the block as used (allocated) or free
         * @param used true to mark as allocated, false to mark as free
         * @note Preserves the size value in the lower bits
         */
        void SetUsed(bool used)
        {
            if (used)
                _usedAndSize |= MemoryAllocatorUtil::HIGHEST_BIT_MASK;
            else
                _usedAndSize &= ~MemoryAllocatorUtil::HIGHEST_BIT_MASK;
        }

        /**
         * @brief Get pointer to the previous block in the physical memory chain
         * @return Pointer to previous node, or nullptr if this is the first block
         * @note This enables backward traversal through the memory pool
         */
        MemoryAllocatorLinkedNode* GetPrevNode() const
        {
            return _pPrev;
        }

        /**
         * @brief Set the pointer to the previous block in the chain
         * @param prev Pointer to the previous node, or nullptr if this is the first block
         */
        void SetPrevNode(MemoryAllocatorLinkedNode* prev)
        {
            _pPrev = prev;
        }

        /**
         * @brief Reset all node data to zero
         * @note Used during initialization or cleanup
         */
        void ClearData()
        {
            _pPrev = nullptr;
            _usedAndSize = 0;
        }

        /**
         * @brief Get pointer to the next physically adjacent block
         * @tparam DefaultAlignment The alignment requirement for blocks
         * @return Pointer to the next block in physical memory
         * @note Calculates position as: current + aligned_header_size + block_size
         * @warning Does not check if the next block is valid or within pool bounds
         */
        template <size_t DefaultAlignment>
        MemoryAllocatorLinkedNode* MoveNext()
        {
            return MemoryAllocatorUtil::PtrOffsetBytes(this, GetSize() + PaddedSize<DefaultAlignment>());
        }

        /**
         * @brief Get the aligned size of this header structure
         * @tparam DefaultAlignment The alignment requirement
         * @return Size of the header aligned to DefaultAlignment boundary
         * @note This is the actual space the header occupies in memory
         * @note User data starts immediately after this padded header size
         */
        template <size_t DefaultAlignment>
        static constexpr size_t PaddedSize()
        {
            return MemoryAllocatorUtil::GetPaddedSize<MemoryAllocatorLinkedNode, DefaultAlignment>();
        }

        /**
         * @brief Recover the node header from a user data pointer
         * @tparam DefaultAlignment The alignment requirement
         * @param ptr Pointer to the user data area (what Allocate() returned)
         * @return Pointer to the node header that precedes the data
         * @note This is the inverse operation of GetDataPtr()
         */
        template <size_t DefaultAlignment>
        static MemoryAllocatorLinkedNode* BackStepToLinkNode(void* ptr)
        {
            return static_cast<MemoryAllocatorLinkedNode*>(MemoryAllocatorUtil::PtrOffsetBytes(ptr, -PaddedSize<DefaultAlignment>()));
        }

    private:
        MemoryAllocatorLinkedNode* _pPrev;     ///< Pointer to previous block in physical memory
        size_t _usedAndSize;                    ///< Combined size (lower bits) and used flag (highest bit)
    };
}
