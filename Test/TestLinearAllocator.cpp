#include <iostream>
#include "Allocator/LinearAllocator.h"

int main()
{
    LinearAllocator allocator(128, 4);

    void* pInt = allocator.Allocate(4);

    std::cout << pInt << std::endl;
}