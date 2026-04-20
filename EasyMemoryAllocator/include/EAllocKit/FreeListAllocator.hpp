#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>

namespace EAllocKit
{
    class FreeListAllocator
    {
    private:
        class LinkedNode
        {
        private:
            static constexpr size_t HIGHEST_BIT_MASK = static_cast<size_t>(1) << (sizeof(size_t) * 8 - 1);

        public:
            static constexpr size_t MAX_ENCODED_SIZE = HIGHEST_BIT_MASK - 1;

            LinkedNode(LinkedNode* prev, size_t size, bool used, uint32_t distance)
                : _pPrev(prev)
                , _usedAndSize(0)
                , _distanceToUserData(distance)
                , _reserved(0)
            {
                SetSize(size);
                SetUsed(used);
            }

            auto GetSize() const -> size_t
            {
                return _usedAndSize & ~HIGHEST_BIT_MASK;
            }

            auto SetSize(size_t size) -> void
            {
                if (size > MAX_ENCODED_SIZE)
                    throw std::overflow_error("FreeListAllocator node size exceeds encodable limit");

                _usedAndSize = (_usedAndSize & HIGHEST_BIT_MASK) | (size & ~HIGHEST_BIT_MASK);
            }

            auto Used() const -> bool
            {
                return (_usedAndSize & HIGHEST_BIT_MASK) != 0;
            }

            auto SetUsed(bool used) -> void
            {
                if (used)
                    _usedAndSize |= HIGHEST_BIT_MASK;
                else
                    _usedAndSize &= ~HIGHEST_BIT_MASK;
            }

            auto GetPrevNode() const -> LinkedNode*
            {
                return _pPrev;
            }

            auto SetPrevNode(LinkedNode* prev) -> void
            {
                _pPrev = prev;
            }

            auto GetDistanceToUserData() const -> uint32_t
            {
                return _distanceToUserData;
            }

            auto SetDistanceToUserData(uint32_t distance) -> void
            {
                _distanceToUserData = distance;
            }

            auto ClearData() -> void
            {
                _pPrev = nullptr;
                _usedAndSize = 0;
                _distanceToUserData = 0;
                _reserved = 0;
            }

        private:
            LinkedNode* _pPrev;
            size_t _usedAndSize;
            uint32_t _distanceToUserData;
            uint32_t _reserved;
        };

        static constexpr size_t DISTANCE_STORAGE_SIZE = sizeof(uint32_t);
        static constexpr size_t NODE_ALIGNMENT = alignof(LinkedNode);
        static constexpr size_t MAX_DISTANCE_TO_USER_DATA = std::numeric_limits<uint32_t>::max();
        static constexpr size_t MIN_ALLOCATABLE_SIZE =
            ((DISTANCE_STORAGE_SIZE + 1 + NODE_ALIGNMENT - 1) / NODE_ALIGNMENT) * NODE_ALIGNMENT;
        static constexpr size_t MIN_DEFAULT_ALIGNMENT = alignof(std::max_align_t);

        static_assert(std::is_trivially_destructible<LinkedNode>::value,
                      "FreeListAllocator LinkedNode must remain trivially destructible");
        static_assert(alignof(LinkedNode) <= alignof(std::max_align_t),
                      "FreeListAllocator LinkedNode alignment exceeds malloc alignment guarantees");

    public:
        explicit FreeListAllocator(size_t size, size_t defaultAlignment = MIN_DEFAULT_ALIGNMENT)
            : _pData(nullptr)
            , _size(size)
            , _defaultAlignment(0)
            , _pFirstNode(nullptr)
        {
            if (!IsPowerOfTwo(defaultAlignment))
                throw std::invalid_argument("FreeListAllocator defaultAlignment must be a power of 2");

            _defaultAlignment = NormalizeDefaultAlignment(defaultAlignment);

            const size_t headerSize = sizeof(LinkedNode);
            const size_t minPayloadSize = AlignUpUnchecked(DISTANCE_STORAGE_SIZE + _defaultAlignment, NODE_ALIGNMENT);
            size_t minSize = 0;
            if (!TryAdd(headerSize, minPayloadSize, minSize))
                throw std::overflow_error("FreeListAllocator minimum size overflow");

            if (_size < minSize)
                _size = minSize;

            size_t maxSupportedSize = 0;
            if (!TryAdd(headerSize, LinkedNode::MAX_ENCODED_SIZE, maxSupportedSize))
                throw std::overflow_error("FreeListAllocator maximum size overflow");

            if (_size > maxSupportedSize)
                throw std::overflow_error("FreeListAllocator size exceeds encodable limit");

            _pData = static_cast<uint8_t*>(::malloc(_size));
            if (!_pData)
                throw std::bad_alloc();

            _pFirstNode = ConstructNode(_pData, nullptr, _size - headerSize, false, 0);
        }

        ~FreeListAllocator()
        {
            if (_pData)
            {
                ::free(_pData);
                _pData = nullptr;
            }
        }

        FreeListAllocator(const FreeListAllocator& rhs) = delete;
        FreeListAllocator(FreeListAllocator&& rhs) = delete;

    public:
        auto Allocate(size_t size) -> void*
        {
            return Allocate(size, _defaultAlignment);
        }

        auto Allocate(size_t size, size_t alignment) -> void*
        {
            if (size == 0)
                return nullptr;

            if (!IsPowerOfTwo(alignment))
                throw std::invalid_argument("FreeListAllocator only supports power-of-2 alignments");

            LinkedNode* pCurrentNode = _pFirstNode;
            while (pCurrentNode != nullptr)
            {
                if (!pCurrentNode->Used())
                {
                    size_t totalNeeded = 0;
                    size_t alignedUserAddr = 0;
                    uint32_t distanceToUserData = 0;
                    if (TryBuildAllocationLayout(pCurrentNode, size, alignment, totalNeeded, alignedUserAddr, distanceToUserData) &&
                        pCurrentNode->GetSize() >= totalNeeded)
                    {
                        const size_t originalSize = pCurrentNode->GetSize();
                        pCurrentNode->SetUsed(true);
                        pCurrentNode->SetDistanceToUserData(distanceToUserData);

                        void* pAlignedUserData = reinterpret_cast<void*>(alignedUserAddr);
                        StoreDistance(pAlignedUserData, distanceToUserData);

                        const size_t remainderSize = originalSize - totalNeeded;
                        if (CanSplit(remainderSize))
                        {
                            pCurrentNode->SetSize(totalNeeded);

                            LinkedNode* pNextNode = ConstructNode(reinterpret_cast<uint8_t*>(pCurrentNode) + sizeof(LinkedNode) + totalNeeded,
                                                                  pCurrentNode,
                                                                  remainderSize - sizeof(LinkedNode),
                                                                  false,
                                                                  0);

                            LinkedNode* pFollowingNode = GetNextNode(pNextNode);
                            if (pFollowingNode != nullptr)
                                pFollowingNode->SetPrevNode(pNextNode);
                        }

                        return pAlignedUserData;
                    }
                }

                pCurrentNode = GetNextNode(pCurrentNode);
            }

            return nullptr;
        }

        auto Deallocate(void* p) -> void
        {
            if (!p)
                return;

            LinkedNode* pCurrentNode = ValidateAndGetHeader(p);
            pCurrentNode->SetUsed(false);

            while (true)
            {
                LinkedNode* pNextNode = GetNextNode(pCurrentNode);
                if (pNextNode == nullptr || pNextNode->Used())
                    break;

                size_t mergedSize = 0;
                if (!TryAddThree(pCurrentNode->GetSize(), sizeof(LinkedNode), pNextNode->GetSize(), mergedSize))
                    throw std::overflow_error("FreeListAllocator merge overflow");

                pCurrentNode->SetSize(mergedSize);
                DestroyNode(pNextNode);

                LinkedNode* pFollowingNode = GetNextNode(pCurrentNode);
                if (pFollowingNode != nullptr)
                    pFollowingNode->SetPrevNode(pCurrentNode);
            }

            while (true)
            {
                LinkedNode* pPrevNode = pCurrentNode->GetPrevNode();
                if (!IsValidHeader(pPrevNode) || pPrevNode->Used())
                    break;

                size_t mergedSize = 0;
                if (!TryAddThree(pPrevNode->GetSize(), sizeof(LinkedNode), pCurrentNode->GetSize(), mergedSize))
                    throw std::overflow_error("FreeListAllocator merge overflow");

                pPrevNode->SetSize(mergedSize);
                DestroyNode(pCurrentNode);

                LinkedNode* pNextNode = GetNextNode(pPrevNode);
                if (pNextNode != nullptr)
                    pNextNode->SetPrevNode(pPrevNode);

                pCurrentNode = pPrevNode;
            }
        }

        auto GetMemoryBlockPtr() const -> void*
        {
            return _pData;
        }

        auto GetFirstNode() const -> LinkedNode*
        {
            return _pFirstNode;
        }

    private:
        static auto StoreDistance(void* userPtr, uint32_t distance) -> void
        {
            uint8_t* distanceStorage = PtrOffsetBytes(static_cast<uint8_t*>(userPtr), -static_cast<std::ptrdiff_t>(DISTANCE_STORAGE_SIZE));
            std::memcpy(distanceStorage, &distance, sizeof(distance));
        }

        static auto ReadDistance(const void* userPtr) -> uint32_t
        {
            uint32_t distance = 0;
            const uint8_t* distanceStorage = PtrOffsetBytes(static_cast<const uint8_t*>(userPtr), -static_cast<std::ptrdiff_t>(DISTANCE_STORAGE_SIZE));
            std::memcpy(&distance, distanceStorage, sizeof(distance));
            return distance;
        }

        auto ValidateAndGetHeader(void* userPtr) const -> LinkedNode*
        {
            const size_t userAddr = ToAddr(static_cast<const uint8_t*>(userPtr));
            const size_t dataBeginAddr = ToAddr(_pData);
            const size_t dataEndAddr = dataBeginAddr + _size;
            const size_t minimumUserAddr = dataBeginAddr + sizeof(LinkedNode) + DISTANCE_STORAGE_SIZE;

            if (userAddr < minimumUserAddr || userAddr >= dataEndAddr)
                throw std::invalid_argument("FreeListAllocator pointer was not allocated by this allocator");

            const uint32_t distanceToUserData = ReadDistance(userPtr);
            if (distanceToUserData < sizeof(LinkedNode) + DISTANCE_STORAGE_SIZE || distanceToUserData > userAddr - dataBeginAddr)
                throw std::invalid_argument("FreeListAllocator pointer was not allocated by this allocator");

            const size_t headerAddr = userAddr - distanceToUserData;
            if (headerAddr > dataEndAddr - sizeof(LinkedNode))
                throw std::invalid_argument("FreeListAllocator pointer was not allocated by this allocator");

            LinkedNode* pHeader = FindNodeByAddress(headerAddr);
            if (pHeader == nullptr)
                throw std::invalid_argument("FreeListAllocator pointer was not allocated by this allocator");

            if (pHeader->GetDistanceToUserData() != distanceToUserData)
                throw std::invalid_argument("FreeListAllocator pointer was not allocated by this allocator");

            if (!pHeader->Used())
                throw std::invalid_argument("FreeListAllocator pointer was already freed");

            if (headerAddr + pHeader->GetDistanceToUserData() != userAddr)
                throw std::invalid_argument("FreeListAllocator pointer was not allocated by this allocator");

            return pHeader;
        }

        auto TryBuildAllocationLayout(const LinkedNode* pNode,
                                      size_t requestedSize,
                                      size_t alignment,
                                      size_t& totalNeeded,
                                      size_t& alignedUserAddr,
                                      uint32_t& distanceToUserData) const -> bool
        {
            size_t afterHeaderAddr = 0;
            if (!TryAdd(ToAddr(pNode), sizeof(LinkedNode), afterHeaderAddr))
                return false;

            size_t minimumUserAddr = 0;
            if (!TryAdd(afterHeaderAddr, DISTANCE_STORAGE_SIZE, minimumUserAddr))
                return false;

            if (!TryAlignUp(minimumUserAddr, alignment, alignedUserAddr))
                return false;

            const size_t prefixSize = alignedUserAddr - afterHeaderAddr;
            size_t rawTotalNeeded = 0;
            if (!TryAdd(prefixSize, requestedSize, rawTotalNeeded))
                return false;

            const size_t rawDistance = alignedUserAddr - ToAddr(pNode);
            if (rawDistance > MAX_DISTANCE_TO_USER_DATA)
                return false;

            if (!TryAlignUp(rawTotalNeeded, NODE_ALIGNMENT, totalNeeded))
                return false;

            if (totalNeeded > LinkedNode::MAX_ENCODED_SIZE)
                return false;

            distanceToUserData = static_cast<uint32_t>(rawDistance);
            return true;
        }

        auto GetNextNode(LinkedNode* pNode) const -> LinkedNode*
        {
            if (!IsValidHeader(pNode))
                return nullptr;

            const size_t nodeAddr = ToAddr(pNode);
            const size_t dataEndAddr = ToAddr(_pData) + _size;
            const size_t headerSize = sizeof(LinkedNode);
            const size_t payloadSize = pNode->GetSize();

            if (nodeAddr > dataEndAddr - headerSize)
                return nullptr;

            const size_t payloadAddr = nodeAddr + headerSize;
            if (payloadSize > dataEndAddr - payloadAddr)
                return nullptr;

            const size_t nextNodeAddr = payloadAddr + payloadSize;
            if (nextNodeAddr == dataEndAddr)
                return nullptr;

            if (nextNodeAddr > dataEndAddr - headerSize)
                return nullptr;

            LinkedNode* pNextNode = reinterpret_cast<LinkedNode*>(nextNodeAddr);
            return IsValidHeader(pNextNode) ? pNextNode : nullptr;
        }

        auto IsValidHeader(const LinkedNode* pHeader) const -> bool
        {
            if (pHeader == nullptr || _pData == nullptr)
                return false;

            const size_t dataBeginAddr = ToAddr(_pData);
            const size_t dataEndAddr = dataBeginAddr + _size;
            const size_t headerStartAddr = ToAddr(pHeader);
            const size_t headerEndAddr = headerStartAddr + sizeof(LinkedNode);
            return headerStartAddr >= dataBeginAddr &&
                   headerEndAddr <= dataEndAddr &&
                   (headerStartAddr % NODE_ALIGNMENT) == 0;
        }

        auto FindNodeByAddress(size_t headerAddr) const -> LinkedNode*
        {
            LinkedNode* pCurrentNode = _pFirstNode;
            while (pCurrentNode != nullptr)
            {
                const size_t currentAddr = ToAddr(pCurrentNode);
                if (currentAddr == headerAddr)
                    return pCurrentNode;

                LinkedNode* pNextNode = GetNextNode(pCurrentNode);
                if (pNextNode != nullptr && ToAddr(pNextNode) <= currentAddr)
                    break;

                pCurrentNode = pNextNode;
            }

            return nullptr;
        }

    private:
        static auto NormalizeDefaultAlignment(size_t alignment) -> size_t
        {
            return std::max(alignment, MIN_DEFAULT_ALIGNMENT);
        }

        static auto IsPowerOfTwo(size_t value) -> bool
        {
            return value > 0 && (value & (value - 1)) == 0;
        }

        static auto CanSplit(size_t remainderSize) -> bool
        {
            return remainderSize >= sizeof(LinkedNode) + MIN_ALLOCATABLE_SIZE;
        }

        static auto ConstructNode(void* storage, LinkedNode* prev, size_t size, bool used, uint32_t distance) -> LinkedNode*
        {
            return std::launder(::new (storage) LinkedNode(prev, size, used, distance));
        }

        static auto DestroyNode(LinkedNode* pNode) -> void
        {
            if (pNode != nullptr)
                pNode->~LinkedNode();
        }

        static auto TryAdd(size_t lhs, size_t rhs, size_t& result) -> bool
        {
            if (lhs > std::numeric_limits<size_t>::max() - rhs)
                return false;

            result = lhs + rhs;
            return true;
        }

        static auto TryAddThree(size_t first, size_t second, size_t third, size_t& result) -> bool
        {
            size_t partial = 0;
            return TryAdd(first, second, partial) && TryAdd(partial, third, result);
        }

        static auto TryAlignUp(size_t value, size_t alignment, size_t& result) -> bool
        {
            const size_t mask = alignment - 1;
            if (value > std::numeric_limits<size_t>::max() - mask)
                return false;

            result = (value + mask) & ~mask;
            return true;
        }

        static constexpr auto AlignUpUnchecked(size_t value, size_t alignment) -> size_t
        {
            return (value + alignment - 1) & ~(alignment - 1);
        }

        template <typename T>
        static auto ToAddr(const T* p) -> size_t
        {
            return reinterpret_cast<size_t>(p);
        }

        template <typename T>
        static auto PtrOffsetBytes(T* ptr, std::ptrdiff_t offset) -> T*
        {
            return reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(ptr) + offset);
        }

        template <typename T>
        static auto PtrOffsetBytes(const T* ptr, std::ptrdiff_t offset) -> const T*
        {
            return reinterpret_cast<const T*>(reinterpret_cast<const uint8_t*>(ptr) + offset);
        }

    private:
        uint8_t* _pData;
        size_t _size;
        size_t _defaultAlignment;
        LinkedNode* _pFirstNode;
    };
}
