#pragma once

#include <cstddef>

class Util
{
public:
    Util() = delete;

public:
    
    inline static size_t UpAlignment(size_t size, size_t alignment)
    {
        return (size + alignment - 1) & ~(alignment - 1);
    }

    template <typename T>
    inline static size_t GetPaddedSize(size_t alignment)
    {
        return Util::UpAlignment(sizeof(T), alignment);
    }
};