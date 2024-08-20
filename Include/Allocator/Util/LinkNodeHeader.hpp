#pragma once

#include "Util.hpp"

namespace MemoryPool
{
    class LinkNodeHeader
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

        LinkNodeHeader* GetPrevNode() const
        {
            return _pPrev;
        }

        void SetPrevNode(LinkNodeHeader* prev)
        {
            _pPrev = prev;
        }

    public:
        static size_t PaddedSize(size_t alignment)
        {
            return Util::GetPaddedSize<LinkNodeHeader>(alignment);
        }

    private:
        LinkNodeHeader* _pPrev;
        size_t _usedAndSize;
    };
}
