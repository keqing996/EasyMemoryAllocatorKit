#pragma once

#include "Util.hpp"

namespace MemoryPool
{
    class LinkNode
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

        LinkNode* GetPrevNode() const
        {
            return _pPrev;
        }

        void SetPrevNode(LinkNode* prev)
        {
            _pPrev = prev;
        }

        void ClearData()
        {
            _pPrev = nullptr;
            _usedAndSize = 0;
        }

        template <size_t DefaultAlignment>
        LinkNode* MoveNext()
        {
            return Util::PtrOffsetBytes(this, GetSize() + PaddedSize<DefaultAlignment>());
        }

    public:
        template <size_t DefaultAlignment>
        static constexpr size_t PaddedSize()
        {
            return Util::GetPaddedSize<LinkNode, DefaultAlignment>();
        }

        template <size_t DefaultAlignment>
        static LinkNode* BackStepToLinkNode(void* ptr)
        {
            return static_cast<LinkNode*>(Util::PtrOffsetBytes(ptr, -PaddedSize<DefaultAlignment>()));
        }

    private:
        LinkNode* _pPrev;
        size_t _usedAndSize;
    };
}
