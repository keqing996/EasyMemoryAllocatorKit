
#include <new>
#include "DocTest.h"
#include "Allocator/LinearAllocator.hpp"
#include "Helper.h"

using namespace MemoryPool;

template<typename T, size_t alignment, size_t blockSize>
void AllocateAndDelete(size_t* alreadyAllocateSize, LinearAllocator<alignment>* pAllocator)
{
    T* pUint1 = CUSTOM_NEW<T>();
    size_t allocationSize = Util::UpAlignment<sizeof(uint32_t), alignment>();

    if (*alreadyAllocateSize + allocationSize > blockSize)
    {
        CHECK(pUint1 == nullptr);
    }
    else
    {
        void* pMemBlock = pAllocator->GetMemoryBlockPtr();
        void* pCurrent = pAllocator->GetCurrentPtr();
        CHECK(ToAddr(pCurrent) == ToAddr(pMemBlock) + allocationSize);
        CUSTOM_DELETE(pUint1);
        CHECK(ToAddr(pCurrent) == ToAddr(pMemBlock) + Util::UpAlignment<sizeof(uint32_t), alignment>());

        *alreadyAllocateSize += allocationSize;
    }
}

template <size_t alignment, size_t blockSize>
void TestAllocation()
{
    AllocatorScope<LinearAllocator<alignment>> allocator(blockSize);

    auto* pAllocator = AllocatorScope<LinearAllocator<alignment>>::CastAllocator();

    void* pMemBlock = pAllocator->GetMemoryBlockPtr();
    void* pCurrent = pAllocator->GetCurrentPtr();
    size_t alreadyAllocateSize = 0;

    PrintPtrAddr("Mem block addr:", pMemBlock);
    CHECK(pMemBlock != nullptr);
    CHECK(pMemBlock == pCurrent);

    AllocateAndDelete<uint32_t, alignment, blockSize>(&alreadyAllocateSize, pAllocator);
    AllocateAndDelete<uint32_t, alignment, blockSize>(&alreadyAllocateSize, pAllocator);
    AllocateAndDelete<uint64_t, alignment, blockSize>(&alreadyAllocateSize, pAllocator);
    AllocateAndDelete<Data64B, alignment, blockSize>(&alreadyAllocateSize, pAllocator);
    AllocateAndDelete<Data64B, alignment, blockSize>(&alreadyAllocateSize, pAllocator);
}

TEST_CASE("TestApi")
{
    TestAllocation<4, 128>();
    TestAllocation<4, 256>();
    TestAllocation<4, 512>();
    TestAllocation<8, 128>();
    TestAllocation<8, 256>();
    TestAllocation<8, 512>();
}