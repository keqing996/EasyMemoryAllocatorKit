#pragma once

#include <cstdlib>

namespace EAllocKit
{
    class MemoryAllocatorUtil
    {
    public:
        MemoryAllocatorUtil() = delete;

    public:
        static constexpr size_t HIGHEST_BIT_MASK = static_cast<size_t>(1) << (sizeof(size_t) * 8 - 1);

    public:
        template <typename T>
        static size_t ToAddr(const T* p)
        {
            return reinterpret_cast<size_t>(p);
        }

        static size_t UpAlignment(size_t size, size_t alignment)
        {
            return (size + alignment - 1) & ~(alignment - 1);
        }

        template <size_t size, size_t alignment>
        constexpr static size_t UpAlignment()
        {
            return (size + alignment - 1) & ~(alignment - 1);
        }

        template <typename T>
        static size_t GetPaddedSize(size_t alignment)
        {
            return UpAlignment(sizeof(T), alignment);
        }

        template <typename T, size_t alignment>
        constexpr static size_t GetPaddedSize()
        {
            return UpAlignment<sizeof(T), alignment>();
        }

        static size_t UpAlignmentPowerOfTwo(size_t value)
        {
            if (value <= 4)
                return 4;

            value--;
            value |= value >> 1;
            value |= value >> 2;
            value |= value >> 4;
            value |= value >> 8;
            value |= value >> 16;
            if constexpr (sizeof(size_t) > 4)
                value |= value >> 32;

            return value + 1;
        }

        template <typename T>
        static T* PtrOffsetBytes(T* ptr, size_t offset)
        {
            return reinterpret_cast<T*>(reinterpret_cast<size_t>(ptr) + offset);
        }
    };
}