#pragma once

namespace MemoryPool
{
    class Allocator
    {
    public:
        virtual ~Allocator() = default;
        virtual void* Allocate(size_t size) = 0;
        virtual void* Allocate(size_t size, size_t alignment) = 0;
        virtual void Deallocate(void* p) = 0;
    };
}