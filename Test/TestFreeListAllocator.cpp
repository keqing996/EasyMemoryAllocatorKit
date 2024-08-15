#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "Allocator/FreeListAllocator.h"
#include "../Src/Util.hpp"
#include "Helper.h"

FreeListAllocator* gAllocator = nullptr;

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
        gAllocator = new(::malloc(sizeof(FreeListAllocator))) FreeListAllocator(blockSize, alignment);
    }

    ~AllocatorScope()
    {
        gAllocator->~FreeListAllocator();
        ::free(gAllocator);

        gAllocator = nullptr;
    }
};

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

    auto pFirstNode = gAllocator->GetBlockFirstNodePtr(pFirstBlock);
    PrintPtrAddr("Block node start addr:", pFirstNode);

    auto pNodeStartAddr = gAllocator->GetNodeStartPtr(pFirstNode);
    PrintPtrAddr("Node start addr:", pNodeStartAddr);

    size_t blockSize = ToAddr(pStartAddr) - ToAddr(pFirstBlock);
    printf("Block size: %zu\n", blockSize);
    CHECK(blockSize == Util::GetPaddedSize<FreeListAllocator::BlockHeader>(alignment));

    size_t nodeSize = ToAddr(pNodeStartAddr) - ToAddr(pFirstNode);
    printf("Node size: %zu\n", nodeSize);
    CHECK(nodeSize == Util::GetPaddedSize<FreeListAllocator::NodeHeader>(alignment));
}

