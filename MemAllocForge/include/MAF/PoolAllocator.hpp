#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
#include "MAFConfig.hpp"

namespace MAF
{
    /// @brief Fixed-size block pool allocator with O(1) alloc/free.
    /// @details Pre-allocates a contiguous array of equal-sized blocks. Allocation pops from
    ///          a free list; deallocation pushes back. Not thread-safe.
    /// @param blockSize Size of each block in bytes.
    /// @param blockNum Number of blocks in the pool.
    /// @param defaultAlignment Block alignment (must be power of 2, >= alignof(max_align_t)).
    /// @throws std::invalid_argument If blockSize is 0 or alignment is not a power of 2.
    class PoolAllocator
    {
    public:
        struct Node
        {
            Node() = default;

            auto GetNext() const -> const Node*
            {
                return _pNext;
            }

        private:
            friend class PoolAllocator;

            explicit Node(Node* pNext)
                : _pNext(pNext)
            {
            }

            Node* _pNext = nullptr;
        };

        explicit PoolAllocator(size_t blockSize, size_t blockNum, size_t defaultAlignment = alignof(std::max_align_t));
        ~PoolAllocator();

        PoolAllocator(const PoolAllocator& rhs) = delete;
        PoolAllocator(PoolAllocator&& rhs) = delete;
        PoolAllocator& operator=(const PoolAllocator&) = delete;
        PoolAllocator& operator=(PoolAllocator&&) = delete;

    public:
        // PoolAllocator is not thread-safe; shared access requires external synchronization.
        auto Allocate() -> void*;
        // Invalid, foreign, misaligned, or already-free pointers are ignored.
        auto Deallocate(void* p) -> void;
        auto GetAvailableBlockCount() const -> size_t;
        auto GetFreeListHeadNode() const -> const Node*;
        auto GetCapacity() const -> size_t;
        auto GetUsedSpace() const -> size_t;
        auto GetFreeSpace() const -> size_t;

    private: // Util functions
        static auto IsPowerOfTwo(size_t value) -> bool
        {
            return value > 0 && (value & (value - 1)) == 0;
        }

        static auto AddWillOverflow(size_t lhs, size_t rhs) -> bool
        {
            return lhs > std::numeric_limits<size_t>::max() - rhs;
        }

        static auto MulWillOverflow(size_t lhs, size_t rhs) -> bool
        {
            return rhs != 0 && lhs > std::numeric_limits<size_t>::max() / rhs;
        }

        static auto TryAlignUp(size_t value, size_t alignment, size_t& alignedValue) -> bool
        {
            const size_t mask = alignment - 1;
            if (value > std::numeric_limits<size_t>::max() - mask)
                return false;

            alignedValue = (value + mask) & ~mask;
            return true;
        }

        auto GetBlockAddress(size_t index) const -> uint8_t*
        {
            return _pData + index * _blockStride;
        }

        auto GetNodeFromIndex(size_t index) const -> Node*
        {
            return std::launder(reinterpret_cast<Node*>(GetBlockAddress(index)));
        }

        auto ConstructFreeNode(size_t index, Node* pNext) -> Node*
        {
            return ::new (static_cast<void*>(GetBlockAddress(index))) Node(pNext);
        }

        auto TryGetBlockIndex(const void* p, size_t& index) const -> bool;
        auto InitializeFreeList() -> void;

    private:
        uint8_t* _pAllocation;
        uint8_t* _pData;
        uint8_t* _pBlockStates;
        size_t _blockSize;
        size_t _blockNum;
        size_t _defaultAlignment;
        size_t _blockStride;
        size_t _availableBlockCount;
        Node* _pFreeBlockList;
    };

    inline PoolAllocator::PoolAllocator(size_t blockSize, size_t blockNum, size_t defaultAlignment)
        : _pAllocation(nullptr)
        , _pData(nullptr)
        , _pBlockStates(nullptr)
        , _blockSize(blockSize)
        , _blockNum(blockNum)
        , _defaultAlignment(defaultAlignment)
        , _blockStride(0)
        , _availableBlockCount(0)
        , _pFreeBlockList(nullptr)
    {
        if (_blockSize == 0)
            throw std::invalid_argument("PoolAllocator blockSize must be greater than 0");

        if (!IsPowerOfTwo(_defaultAlignment))
            throw std::invalid_argument("PoolAllocator defaultAlignment must be a power of 2");

        if (_defaultAlignment < alignof(std::max_align_t))
            _defaultAlignment = alignof(std::max_align_t);

        if (_defaultAlignment < alignof(Node))
            _defaultAlignment = alignof(Node);

        const size_t minBlockSize = _blockSize < sizeof(Node) ? sizeof(Node) : _blockSize;
        if (!TryAlignUp(minBlockSize, _defaultAlignment, _blockStride))
            throw std::overflow_error("PoolAllocator block stride overflow");

        if (_blockNum == 0)
            return;

        if (MulWillOverflow(_blockStride, _blockNum))
            throw std::overflow_error("PoolAllocator pool size overflow");

        const size_t poolBytes = _blockStride * _blockNum;
        const size_t stateBytes = _blockNum;
        if (AddWillOverflow(poolBytes, stateBytes))
            throw std::overflow_error("PoolAllocator pool size overflow");

        const size_t totalBytes = poolBytes + stateBytes;
        _pAllocation = static_cast<uint8_t*>(::operator new(totalBytes, std::align_val_t(_defaultAlignment)));
        _pData = _pAllocation;
        _pBlockStates = _pData + poolBytes;

        InitializeFreeList();
    }

    inline PoolAllocator::~PoolAllocator()
    {
        if (_pAllocation != nullptr)
        {
            ::operator delete(_pAllocation, std::align_val_t(_defaultAlignment));
            _pAllocation = nullptr;
        }

        _pData = nullptr;
        _pBlockStates = nullptr;
        _pFreeBlockList = nullptr;
        _availableBlockCount = 0;
    }

    inline auto PoolAllocator::Allocate() -> void*
    {
        if (_pFreeBlockList == nullptr)
            return nullptr;

        Node* pResult = _pFreeBlockList;
        _pFreeBlockList = _pFreeBlockList->_pNext;

        const size_t blockIndex = static_cast<size_t>(reinterpret_cast<uint8_t*>(pResult) - _pData) / _blockStride;
        _pBlockStates[blockIndex] = 0;
        --_availableBlockCount;

        return pResult;
    }

    inline auto PoolAllocator::Deallocate(void* p) -> void
    {
        if (p == nullptr)
            return;

        size_t blockIndex = 0;
        if (!TryGetBlockIndex(p, blockIndex))
        {
            MAF_DEALLOC_FAIL("PoolAllocator pointer does not belong to this allocator");
        }

        if (_pBlockStates[blockIndex] != 0)
        {
            MAF_DEALLOC_FAIL("PoolAllocator pointer is not an active allocation (possible double-free)");
        }

        _pFreeBlockList = ConstructFreeNode(blockIndex, _pFreeBlockList);
        _pBlockStates[blockIndex] = 1;
        ++_availableBlockCount;
    }

    inline auto PoolAllocator::GetAvailableBlockCount() const -> size_t
    {
        return _availableBlockCount;
    }

    inline auto PoolAllocator::GetFreeListHeadNode() const -> const Node*
    {
        return _pFreeBlockList;
    }

    inline auto PoolAllocator::GetCapacity() const -> size_t
    {
        return _blockStride * _blockNum;
    }

    inline auto PoolAllocator::GetUsedSpace() const -> size_t
    {
        return (_blockNum - _availableBlockCount) * _blockStride;
    }

    inline auto PoolAllocator::GetFreeSpace() const -> size_t
    {
        return _availableBlockCount * _blockStride;
    }

    inline auto PoolAllocator::TryGetBlockIndex(const void* p, size_t& index) const -> bool
    {
        if (_pData == nullptr || _blockNum == 0)
            return false;

        const uintptr_t dataBegin = reinterpret_cast<uintptr_t>(_pData);
        const uintptr_t poolBytes = _blockStride * _blockNum;
        const uintptr_t ptrValue = reinterpret_cast<uintptr_t>(p);

        if (ptrValue < dataBegin)
            return false;

        const size_t offset = ptrValue - dataBegin;
        if (offset >= poolBytes)
            return false;

        if (offset % _blockStride != 0)
            return false;

        index = offset / _blockStride;
        return index < _blockNum;
    }

    inline auto PoolAllocator::InitializeFreeList() -> void
    {
        _availableBlockCount = _blockNum;
        _pFreeBlockList = nullptr;
        for (size_t i = _blockNum; i > 0; --i)
        {
            const size_t blockIndex = i - 1;
            _pFreeBlockList = ConstructFreeNode(blockIndex, _pFreeBlockList);
            _pBlockStates[blockIndex] = 1;
        }
    }
}
