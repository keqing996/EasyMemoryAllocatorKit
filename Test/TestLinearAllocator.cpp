
#include <new>
#include "DocTest.h"
#include "Allocator/LinearAllocator.hpp"
#include "Helper.h"

using namespace MemoryPool;

struct AllocatorMarker {};
inline void* operator new(size_t, AllocatorMarker, void* ptr) { return ptr; }
inline void operator delete(void*, AllocatorMarker, void*) { }

template <size_t DefaultAlignment = 4>
struct AllocatorScope
{
    LinearAllocator<DefaultAlignment>* gAllocator = nullptr;

    explicit AllocatorScope(size_t blockSize)
    {
        void* pAllocator = ::malloc(sizeof(LinearAllocator<DefaultAlignment>));
        gAllocator = new(pAllocator) LinearAllocator<DefaultAlignment>(blockSize);
    }

    ~AllocatorScope()
    {
        gAllocator->~LinearAllocator();
        ::free(gAllocator);

        gAllocator = nullptr;
    }
};

template<typename T, size_t DefaultAlignment>
T* CUSTOM_NEW(AllocatorScope<DefaultAlignment>& allocator)
{
    return new (AllocatorMarker(), allocator.gAllocator->Allocate(sizeof(T))) T();
}

template<typename T, size_t DefaultAlignment, typename... Args>
T* CUSTOM_NEW(AllocatorScope<DefaultAlignment>& allocator, Args&&... args)
{
    return new (AllocatorMarker(), allocator.gAllocator->Allocate(sizeof(T))) T(std::forward<Args>(args)...);
}

template<typename T, size_t DefaultAlignment>
void CUSTOM_DELETE(AllocatorScope<DefaultAlignment>& allocator, T* p)
{
    if (!p)
        return;
    p->~T();
    allocator.gAllocator->Deallocate(p);
}

/*
template <size_t Alignment>
void TestBasicAllocation(size_t alignment)
{
    AllocatorScope<Alignment> allocator(128);

    uint32_t* pUint = CUSTOM_NEW<uint32_t, Alignment>(allocator);
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
*/

TEST_CASE("TestApi")
{
    printf("======= Test Basic Creation =======\n");

    constexpr size_t alignment = 4;
    AllocatorScope<alignment> allocator(128);

    uint32_t* pUint = CUSTOM_NEW<uint32_t>(allocator);
    CHECK(pUint != nullptr);
}

/*

TEST_CASE("TestAllocate")
{
    printf("======= Test Basic Allocation (align = 4) =======\n");

    TestBasicAllocation(4, 4);

    printf("======= Test Basic Allocation (align = 8) =======\n");

    TestBasicAllocation(8, 8);

    printf("======= Test Basic Allocation (align = 9 -> 16) =======\n");

    TestBasicAllocation(9, 16);
}

*/