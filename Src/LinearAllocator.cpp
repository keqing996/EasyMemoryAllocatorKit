
#include <cstdlib>
#include "Allocator/LinearAllocator.h"
#include "Util.hpp"

LinearAllocator::LinearAllocator(size_t size)
    : _pData(static_cast<uint8_t*>(::malloc(size)))
    , _size(size)
{
}

LinearAllocator::~LinearAllocator()
{
    ::free(_pData);
}

void* LinearAllocator::Allocate(size_t size, size_t alignment)
{
    size_t requiredSize = Util::UpAlignment(size, alignment);
    size_t available = GetAvailableSpace();

    if (available < requiredSize)
       return nullptr;

    void* result = _pCurrent;
    _pCurrent += requiredSize;
    return result;
}

void LinearAllocator::Deallocate(void* p)
{
    // do nothing
}

size_t LinearAllocator::GetAvailableSpace() const
{
    return reinterpret_cast<size_t>(_pData) + _size - reinterpret_cast<size_t>(_pCurrent);
}