#pragma once

#include <cstdlib>
#include <cstddef>
#include <cstdint>

namespace EAllocKit
{
    class Util
    {
    public:
        Util() = delete;
        
        static constexpr size_t HIGHEST_BIT_MASK = static_cast<size_t>(1) << (sizeof(size_t) * 8 - 1);
        
        static size_t RoundUpToPowerOf2(size_t size)
        {
            if (size == 0)
                return 1;

            size--;
            size |= size >> 1;
            size |= size >> 2;
            size |= size >> 4;
            size |= size >> 8;
            size |= size >> 16;

            if constexpr (sizeof(size_t) > 4)
                size |= size >> 32;

            size++;

            return size;
        }
        
        static size_t Log2(size_t value)
        {
            size_t result = 0;
            while (value >>= 1)
                result++;
            return result;
        }
        
        template <typename T>
        static size_t ToAddr(const T* p)
        {
            return reinterpret_cast<size_t>(p);
        }
        
        static bool IsPowerOfTwo(size_t value)
        {
            return value > 0 && (value & (value - 1)) == 0;
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
        
        static size_t GetPaddedSize(size_t size, size_t alignment)
        {
            return UpAlignment(size, alignment);
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
        static T* PtrOffsetBytes(T* ptr, std::ptrdiff_t offset)
        {
            return reinterpret_cast<T*>(static_cast<uint8_t*>(static_cast<void*>(ptr)) + offset);
        }
    };
}