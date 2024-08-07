#pragma once

#include <cstddef>

class FreeListAllocator
{
public:
    FreeListAllocator(size_t blockSize);

private:
    struct BlockHeader
    {
        BlockHeader* pNext;
        size_t size;
    };

    struct NodeHeader
    {
        
    }

};