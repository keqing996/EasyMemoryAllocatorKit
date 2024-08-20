#pragma once
#include <cstdint>

class LinearAllocator
{
public:
    explicit LinearAllocator(size_t size);
    ~LinearAllocator();

    LinearAllocator(const LinearAllocator& rhs) = delete;
    LinearAllocator(LinearAllocator&& rhs) = delete;

public:
    void* Allocate(size_t size, size_t alignment = 4);
    void Deallocate(void* p);
    void Reset();

private:
    size_t GetAvailableSpace() const;

private:
    uint8_t* _pData;
    uint8_t* _pCurrent;
    size_t _size;
};


