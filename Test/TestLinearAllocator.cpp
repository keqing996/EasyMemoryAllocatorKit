
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <iostream>
#include "Allocator/LinearAllocator.h"
#include "../Src/Util.hpp"
#include "Helper.h"

LinearAllocator* gAllocator = nullptr;

// Bypass placement new in case of nonexpect platform
struct AllocatorMarker {};
inline void* operator new(size_t, AllocatorMarker, void* ptr) { return ptr; }
inline void operator delete(void*, AllocatorMarker, void*) { }

template<typename T>
T* CUSTOM_NEW()
{
    return new (AllocatorMarker(), gAllocator->Allocate(sizeof(T))) T();
}

template<typename T, typename... Args>
T* CUSTOM_NEW(Args&&... args)
{
    return new (AllocatorMarker(), gAllocator->Allocate(sizeof(T))) T(std::forward<Args>(args)...);
}

template<typename T>
void CUSTOM_DELETE(T* p)
{
    if (!p)
        return;

    p->~T();
    gAllocator->Deallocate(p);
}

struct AllocatorScope
{
    AllocatorScope(size_t blockSize, size_t alignment)
    {
        gAllocator = new(::malloc(sizeof(LinearAllocator))) LinearAllocator(blockSize, alignment);
    }

    ~AllocatorScope()
    {
        gAllocator->~LinearAllocator();
        ::free(gAllocator);

        gAllocator = nullptr;
    }
};

void TestBasicAllocation(size_t alignment, size_t expectAlignment)
{
    AllocatorScope scope(128, alignment);

    CHECK(gAllocator->GetCurrentAlignment() == expectAlignment);
    alignment = gAllocator->GetCurrentAlignment();

    uint32_t* pUint = CUSTOM_NEW<uint32_t>();
    *pUint = 0xABCDABCD;

    auto pFirstBlock = gAllocator->GetFirstBlockPtr();
    PrintPtrAddr("First block addr:", pFirstBlock);

    auto pStartAddr = gAllocator->GetBlockStartPtr(pFirstBlock);
    PrintPtrAddr("Block start addr:", pStartAddr);

    auto pCurrentAddr = pFirstBlock->pCurrent;
    PrintPtrAddr("After uint32 Current addr", pCurrentAddr);

    CHECK(ToAddr(pCurrentAddr) == ToAddr(pStartAddr) + Util::UpAlignment(sizeof(uint32_t), alignment));
    CHECK(ToAddr(pStartAddr) == ToAddr(pUint));

    struct Temp
    {
        int a;
        float b;
        char c;

        Temp(int aa, float bb, char cc): a(aa), b(bb), c(cc)
        {
        }
    };

    printf("Size of struct: %llu\n", sizeof(Temp));

    void* pLastCurrentAddr = pCurrentAddr;
    Temp* pTemp = CUSTOM_NEW<Temp>(1, 2.0f, 'x');

    pCurrentAddr = pFirstBlock->pCurrent;
    PrintPtrAddr("After temp Current addr", pCurrentAddr);

    CHECK(ToAddr(pLastCurrentAddr) == ToAddr(pTemp));
    CHECK(ToAddr(pCurrentAddr) == ToAddr(pLastCurrentAddr) + Util::UpAlignment(sizeof(Temp), alignment));
}

TEST_CASE("TestApi")
{
    printf("======= Test Basic Creation =======\n");

    size_t alignment = 4;
    AllocatorScope scope(128, alignment);

    CHECK(gAllocator->GetCurrentBlockNum() == 1);

    auto pFirstBlock = gAllocator->GetFirstBlockPtr();
    PrintPtrAddr("First block addr:", pFirstBlock);

    auto pStartAddr = gAllocator->GetBlockStartPtr(pFirstBlock);
    PrintPtrAddr("Block start addr:", pStartAddr);

    size_t blockSize = ToAddr(pStartAddr) - ToAddr(pFirstBlock);
    CHECK(blockSize == Util::GetPaddedSize<LinearAllocator::BlockHeader>(alignment));
}

TEST_CASE("TestAllocate")
{
    printf("======= Test Basic Allocation (align = 4) =======\n");

    TestBasicAllocation(4, 4);

    printf("======= Test Basic Allocation (align = 8) =======\n");

    TestBasicAllocation(8, 8);

    printf("======= Test Basic Allocation (align = 9 -> 16) =======\n");

    TestBasicAllocation(9, 16);
}

TEST_CASE("TestAddBlock")
{
    printf("======= Test Add Blcok =======\n");

    size_t alignment = 8;
    AllocatorScope scope(128, alignment);

    struct Temp
    {
        double number[20];
    };

    printf("Size of struct: %llu\n", sizeof(Temp));

    Temp* pTemp = CUSTOM_NEW<Temp>();

    CHECK(gAllocator->GetCurrentBlockNum() == 2);

    uint32_t* pUint = CUSTOM_NEW<uint32_t>();

    CHECK(gAllocator->GetCurrentBlockNum() == 3);

    auto pFirstBlock = gAllocator->GetFirstBlockPtr();
    auto pSecondBlock = pFirstBlock->pNext;
    CHECK(pSecondBlock->size == Util::UpAlignment(sizeof(Temp), alignment));

    auto pThirdBlock = pFirstBlock->pNext->pNext;
    auto pThirdStart = gAllocator->GetBlockStartPtr(pThirdBlock);

    CHECK(ToAddr(pThirdStart) == ToAddr(pUint));

    Temp* pTemp2 = CUSTOM_NEW<Temp>();

    CHECK(gAllocator->GetCurrentBlockNum() == 4);
}