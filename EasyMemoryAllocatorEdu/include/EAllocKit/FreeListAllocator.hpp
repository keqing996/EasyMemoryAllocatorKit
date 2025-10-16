#pragma once

#include <new>
#include <cstdlib>
#include <stdexcept>

namespace EAllocKit
{
    class FreeListAllocator
    {
    private:
        /**
         * @brief Linked Node Header for Memory Allocators
         * Memory layout:     
         * +------------------+------------------+
         * | Previous Pointer | Size + Used Flag |
         * +------------------+------------------+
         */
        class LinkedNode
        {
        private:
            static constexpr size_t HIGHEST_BIT_MASK = static_cast<size_t>(1) << (sizeof(size_t) * 8 - 1);
            
        public:
            size_t GetSize() const
            {
                return _usedAndSize & ~HIGHEST_BIT_MASK;
            }

            void SetSize(size_t size)
            {
                _usedAndSize = (_usedAndSize & HIGHEST_BIT_MASK) | (size & ~HIGHEST_BIT_MASK);
            }

            bool Used() const
            {
                return (_usedAndSize & HIGHEST_BIT_MASK) != 0;
            }

            void SetUsed(bool used)
            {
                if (used)
                    _usedAndSize |= HIGHEST_BIT_MASK;
                else
                    _usedAndSize &= ~HIGHEST_BIT_MASK;
            }

            LinkedNode* GetPrevNode() const
            {
                return _pPrev;
            }

            void SetPrevNode(LinkedNode* prev)
            {
                _pPrev = prev;
            }

            void ClearData()
            {
                _pPrev = nullptr;
                _usedAndSize = 0;
            }

        private:
            LinkedNode* _pPrev;     ///< Pointer to previous block in physical memory
            size_t _usedAndSize;    ///< Combined size (lower bits) and used flag (highest bit)
        };

    public:
        explicit FreeListAllocator(size_t size, size_t defaultAlignment = 4)
            : _pData(nullptr)
            , _size(size)
            , _defaultAlignment(defaultAlignment)
            , _pFirstNode(nullptr)
        {
            if (!IsPowerOfTwo(defaultAlignment))
                throw std::invalid_argument("FreeListAllocator defaultAlignment must be a power of 2");
                
            size_t headerSize = sizeof(LinkedNode);
            size_t minSize = headerSize + 4 + _defaultAlignment;  // header + distance + minimal aligned data
            if (_size < minSize)
                _size = minSize;

            _pData = static_cast<uint8_t*>(::malloc(_size));
            
            if (!_pData)
                throw std::bad_alloc();

            _pFirstNode = reinterpret_cast<LinkedNode*>(_pData);
            _pFirstNode->SetUsed(false);
            _pFirstNode->SetSize(_size - headerSize);
            _pFirstNode->SetPrevNode(nullptr);
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

        void* Allocate(size_t size)
        {
            return Allocate(size, _defaultAlignment);
        }
        
        void* Allocate(size_t size, size_t alignment)
        {
            if (!IsPowerOfTwo(alignment))
                throw std::invalid_argument("FreeListAllocator only supports power-of-2 alignments");
                
            const size_t headerSize = sizeof(LinkedNode);

            LinkedNode* pCurrentNode = _pFirstNode;
            while (true)
            {
                if (pCurrentNode == nullptr)
                    return nullptr;

                if (!pCurrentNode->Used())
                {
                    // Calculate aligned user data address using StackAllocator's layout
                    size_t nodeStartAddr = ToAddr(pCurrentNode);
                    size_t afterHeaderAddr = nodeStartAddr + headerSize;
                    size_t minimalUserAddr = afterHeaderAddr + 4;  // Reserve 4 bytes for distance
                    size_t alignedUserAddr = UpAlignment(minimalUserAddr, alignment);
                    
                    // Calculate total space needed (excluding header since GetSize() is user data size)
                    size_t totalNeeded = (alignedUserAddr - afterHeaderAddr) + size;
                    
                    if (pCurrentNode->GetSize() >= totalNeeded)
                    {
                        pCurrentNode->SetUsed(true);

                        // Store distance from user pointer back to header
                        void* pAlignedUserData = reinterpret_cast<void*>(alignedUserAddr);
                        uint32_t distance = static_cast<uint32_t>(alignedUserAddr - nodeStartAddr);
                        StoreDistance(pAlignedUserData, distance);

                        // Create a new node if left size is enough to place a new header + distance + minimal data
                        size_t actualUsedSize = totalNeeded;
                        size_t leftSize = pCurrentNode->GetSize() - actualUsedSize;
                        if (leftSize > headerSize + 4)  // Need space for header + distance storage + some data
                        {
                            pCurrentNode->SetSize(actualUsedSize);

                            LinkedNode* pNextNode = reinterpret_cast<LinkedNode*>(reinterpret_cast<char*>(pCurrentNode) + headerSize + actualUsedSize);
                            pNextNode->SetPrevNode(pCurrentNode);
                            pNextNode->SetUsed(false);
                            pNextNode->SetSize(leftSize - headerSize);
                        }

                        return pAlignedUserData;
                    }
                }

                char* nextAddr = reinterpret_cast<char*>(pCurrentNode) + headerSize + pCurrentNode->GetSize();
                pCurrentNode = reinterpret_cast<LinkedNode*>(nextAddr);
                if (!IsValidHeader(pCurrentNode))
                    pCurrentNode = nullptr;
            }
        }
        
        void Deallocate(void* p)
        {
            if (!p)
                return;
                
            // Use distance-based method to get back to the header
            LinkedNode* pCurrentNode = GetHeaderFromUserPtr(p);
            pCurrentNode->SetUsed(false);

            size_t headerSize = sizeof(LinkedNode);

            // Merge forward
            while (true)
            {
                char* nextAddr = reinterpret_cast<char*>(pCurrentNode) + headerSize + pCurrentNode->GetSize();
                LinkedNode* pNextNode = reinterpret_cast<LinkedNode*>(nextAddr);
                if (!IsValidHeader(pNextNode) || pNextNode->Used())
                    break;

                size_t oldSize = pCurrentNode->GetSize();
                size_t newSize = oldSize + headerSize + pNextNode->GetSize();
                pNextNode->ClearData();
                pCurrentNode->SetSize(newSize);
            }

            // Merge backward
            while (true)
            {
                LinkedNode* pPrevNode = pCurrentNode->GetPrevNode();
                if (!IsValidHeader(pPrevNode) || pPrevNode->Used())
                    break;

                // Adjust prev node's size
                const size_t oldSize = pPrevNode->GetSize();
                const size_t newSize = oldSize + headerSize + pCurrentNode->GetSize();
                pPrevNode->SetSize(newSize);

                // Adjust next node's prev
                char* nextAddr = reinterpret_cast<char*>(pCurrentNode) + headerSize + pCurrentNode->GetSize();
                LinkedNode* pNextNode = reinterpret_cast<LinkedNode*>(nextAddr);
                if (IsValidHeader(pNextNode))
                    pNextNode->SetPrevNode(pPrevNode);

                // Clear this node
                pCurrentNode->ClearData();

                // Move backward
                pCurrentNode = pPrevNode;
            }
        }
        
        void* GetMemoryBlockPtr() const
        {
            return _pData;
        }

        LinkedNode* GetFirstNode() const
        {
            return _pFirstNode;
        }

    private:
        static void StoreDistance(void* userPtr, uint32_t distance)
        {
            uint32_t* distPtr = static_cast<uint32_t*>(PtrOffsetBytes(userPtr, -4));
            *distPtr = distance;
        }

        static uint32_t ReadDistance(void* userPtr)
        {
            uint32_t* distPtr = static_cast<uint32_t*>(PtrOffsetBytes(userPtr, -4));
            return *distPtr;
        }

        static LinkedNode* GetHeaderFromUserPtr(void* userPtr)
        {
            uint32_t distance = ReadDistance(userPtr);
            return static_cast<LinkedNode*>(PtrOffsetBytes(userPtr, -static_cast<std::ptrdiff_t>(distance)));
        }

        bool IsValidHeader(const LinkedNode* pHeader) const
        {
            const size_t dataBeginAddr = ToAddr(_pData);
            const size_t dataEndAddr = dataBeginAddr + _size;
            const size_t headerStartAddr = ToAddr(pHeader);
            const size_t headerEndAddr = headerStartAddr + sizeof(LinkedNode);
            return headerStartAddr >= dataBeginAddr && headerEndAddr < dataEndAddr;
        }

    private: // Util functions
        static bool IsPowerOfTwo(size_t value)
        {
            return value > 0 && (value & (value - 1)) == 0;
        }

        static size_t UpAlignment(size_t size, size_t alignment)
        {
            return (size + alignment - 1) & ~(alignment - 1);
        }

        template <typename T>
        static size_t ToAddr(const T* p)
        {
            return reinterpret_cast<size_t>(p);
        }

        template <typename T>
        static T* PtrOffsetBytes(T* ptr, std::ptrdiff_t offset)
        {
            return reinterpret_cast<T*>(static_cast<uint8_t*>(static_cast<void*>(ptr)) + offset);
        }

    private:
        uint8_t* _pData;
        size_t _size;
        size_t _defaultAlignment;
        LinkedNode* _pFirstNode;
    };
}
