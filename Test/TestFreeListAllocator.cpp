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

void TestBasicAllocation(size_t alignment, size_t expectAlignment)
{
    AllocatorScope scope(128, alignment);

    CHECK(gAllocator->GetCurrentAlignment() == expectAlignment);
    alignment = gAllocator->GetCurrentAlignment();

    auto pFirstBlock = gAllocator->GetFirstBlockPtr();
    PrintPtrAddr("First block addr:", pFirstBlock);

    auto pFirstBlockStart = gAllocator->GetBlockStartPtr(pFirstBlock);
    PrintPtrAddr("First block start addr:", pFirstBlockStart);

    auto pFirstNode = gAllocator->GetBlockFirstNodePtr(pFirstBlock);
    PrintPtrAddr("First node addr:", pFirstNode);

    auto pFirstNodeStart = gAllocator->GetNodeStartPtr(pFirstNode);
    PrintPtrAddr("First node start addr:", pFirstNodeStart);

    uint32_t* pUint = CUSTOM_NEW<uint32_t>();
    *pUint = 0xABCDABCD;

    CHECK(ToAddr(pFirstNodeStart) == ToAddr(pUint));
    CHECK(pFirstNode->Used() == true);

    auto pSecondNode = gAllocator->GetNodeNext(pFirstNode);
    PrintPtrAddr("Second node addr:", pSecondNode);

    CHECK(ToAddr(pSecondNode) == ToAddr(pUint) + Util::UpAlignment(sizeof(uint32_t), alignment));

    uint32_t* pUint2 = CUSTOM_NEW<uint32_t>();
    *pUint2 = 0xABCDABCD;

    CHECK(pFirstNode->Used() == true);

    auto pThirdNode = gAllocator->GetNodeNext(pSecondNode);
    PrintPtrAddr("Third node addr:", pThirdNode);

    CUSTOM_DELETE(pUint);

    CHECK(pFirstNode->Used() == false);

    pUint = CUSTOM_NEW<uint32_t>();

    CHECK(ToAddr(pFirstNodeStart) == ToAddr(pUint));
    CHECK(pFirstNode->Used() == true);
    CHECK(gAllocator->GetNodeNext(pFirstNode) == pSecondNode);

    uint32_t* pUint3 = CUSTOM_NEW<uint32_t>();
    *pUint3 = 0xABCDABCD;

    CUSTOM_DELETE(pUint);
    CUSTOM_DELETE(pUint2);

    CHECK(pFirstNode->Used() == false);
    CHECK(gAllocator->GetNodeNext(pFirstNode) == pThirdNode);
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
    printf("======= Test Add Block =======\n");

    size_t alignment = 8;
    AllocatorScope scope(128, alignment);

    struct Temp
    {
        uint32_t data[10];
    };

    // block1: Node(16+40) -> NodeFree(16+56)
    Temp* pTemp1 = CUSTOM_NEW<Temp>();

    // block1: Node(16+40) -> Node(16+40+16)
    Temp* pTemp2 = CUSTOM_NEW<Temp>();

    CHECK(gAllocator->GetCurrentBlockNum() == 1);

    // block1: Node(16+40) -> Node(16+40+16)
    // block2: Node(16+40) -> NodeFree(16+56)
    Temp* pTemp3 = CUSTOM_NEW<Temp>();

    // block1: Node(16+40) -> Node(16+40+16)
    // block2: Node(16+40) -> Node(16+40+16)
    Temp* pTemp4 = CUSTOM_NEW<Temp>();

    CHECK(gAllocator->GetCurrentBlockNum() == 2);

    // block1: Node(16+40) -> Node(16+40+16)
    // block2: Node(16+40) -> Node(16+40+16)
    // block3: Node(16+40) -> NodeFree(16+56)
    Temp* pTemp5 = CUSTOM_NEW<Temp>();

    CHECK(gAllocator->GetCurrentBlockNum() == 3);

    // block1: NodeFree(16+40) -> Node(16+40+16)
    // block2: Node(16+40) -> Node(16+40+16)
    // block3: Node(16+40) -> NodeFree(16+56)
    CUSTOM_DELETE(pTemp1);

    // block1: NodeFree(16+112)
    // block2: Node(16+40) -> Node(16+40+16)
    // block3: Node(16+40) -> NodeFree(16+56)
    CUSTOM_DELETE(pTemp2);

    CHECK(gAllocator->GetCurrentBlockNum() == 3);

    // block1: NodeFree(16+112)
    // block2: NodeFree(16+40) -> Node(16+40+16)
    // block3: Node(16+40) -> NodeFree(16+56)
    CUSTOM_DELETE(pTemp3);

    CHECK(gAllocator->GetCurrentBlockNum() == 3);

    // block1: NodeFree(16+112)
    // block3: Node(16+40) -> NodeFree(16+56)
    CUSTOM_DELETE(pTemp4);

    CHECK(gAllocator->GetCurrentBlockNum() == 2);

    // block1: NodeFree(16+112)
    CUSTOM_DELETE(pTemp5);

    CHECK(gAllocator->GetCurrentBlockNum() == 1);
}

TEST_CASE("TestSplitAndMerge")
{
    printf("======= Test Split & Merge =======\n");

    size_t alignment = 8;
    AllocatorScope scope(256, alignment);

    size_t nodeHeaderSize = Util::GetPaddedSize<FreeListAllocator::NodeHeader>(alignment);

    struct Data24B { uint8_t data[24]; };

    // block1: Node(16 + 24) + NodeFree(16 + 200)
    Data24B* pTestData1 = CUSTOM_NEW<Data24B>();

    // block1: Node(16 + 24) + Node(16 + 24) + NodeFree(16 + 160)
    Data24B* pTestData2 = CUSTOM_NEW<Data24B>();

    // block1: Node(16 + 24) + Node(16 + 24) + Node(16 + 24) + NodeFree(16 + 120)
    Data24B* pTestData3 = CUSTOM_NEW<Data24B>();

    // block1: Node(16 + 24) + Node(16 + 24) + Node(16 + 24) + Node(16 + 24) + NodeFree(16 + 80)
    Data24B* pTestData4 = CUSTOM_NEW<Data24B>();

    // block1: Node(16 + 24) + Node(16 + 24) + Node(16 + 24) + Node(16 + 24) + Node(16 + 24) + NodeFree(16 + 40)
    Data24B* pTestData5 = CUSTOM_NEW<Data24B>();

    // block1: Node(16 + 24) + Node(16 + 24) + NodeFree(16 + 24) + Node(16 + 24) + Node(16 + 24) + NodeFree(16 + 40)
    CUSTOM_DELETE(pTestData3);

    // block1: Node(16 + 24) + NodeFree(16 + 64) + Node(16 + 24) + Node(16 + 24) + NodeFree(16 + 40)
    CUSTOM_DELETE(pTestData2);

    // block1: Node(16 + 24) + NodeFree(16 + 104) + Node(16 + 24) + NodeFree(16 + 40)
    CUSTOM_DELETE(pTestData4);

    auto pFirstBlock = gAllocator->GetFirstBlockPtr();
    auto pFirstNode = gAllocator->GetBlockFirstNodePtr(pFirstBlock);

    CHECK(pFirstNode->Used() == true);
    CHECK(pFirstNode->GetSize() == sizeof(Data24B));
    CHECK(pFirstNode->GetPrevNode() == nullptr);

    auto pSecondNode = gAllocator->GetNodeNext(pFirstNode);
    CHECK(pSecondNode->Used() == false);
    CHECK(pSecondNode->GetSize() == 2 * nodeHeaderSize + 3 * sizeof(Data24B));
    CHECK(pSecondNode->GetPrevNode() == pFirstNode);

    auto pThirdNode = gAllocator->GetNodeNext(pSecondNode);
    CHECK(pThirdNode->Used() == true);
    CHECK(pFirstNode->GetSize() == sizeof(Data24B));
    CHECK(pThirdNode->GetPrevNode() == pSecondNode);

    auto pFourthNode = gAllocator->GetNodeNext(pThirdNode);
    CHECK(pFourthNode->Used() == false);
    CHECK(pFourthNode->GetPrevNode() == pThirdNode);
    CHECK(gAllocator->GetNodeNext(pFourthNode) == nullptr);

    CUSTOM_DELETE(pTestData1);
    CUSTOM_DELETE(pTestData5);

    CHECK(pFirstNode->GetSize() == 256 - nodeHeaderSize);
}

TEST_CASE("TestAll")
{
    printf("======= Test All =======\n");

    size_t alignment = 8;
    AllocatorScope scope(128, alignment);

    size_t nodeHeaderSize = Util::GetPaddedSize<FreeListAllocator::NodeHeader>(alignment);

    struct Data16B { uint8_t data[16]; };
    struct Data24B { uint8_t data[24]; };
    struct Data32B { uint8_t data[32]; };
    struct Data64B { uint8_t data[64]; };

    // block1: Node(16 + 16) + NodeFree(16 + 80)
    Data16B* pBlock1Data16_1 = CUSTOM_NEW<Data16B>();

    {
        CHECK(gAllocator->GetCurrentBlockNum() == 1);

        auto pFirstBlock = gAllocator->GetFirstBlockPtr();
        auto pFirstNode = gAllocator->GetBlockFirstNodePtr(pFirstBlock);

        CHECK(pFirstNode->Used() == true);
        CHECK(pFirstNode->GetSize() == sizeof(Data16B));

        auto pSecondNode = gAllocator->GetNodeNext(pFirstNode);

        CHECK(pSecondNode->Used() == false);
        CHECK(gAllocator->GetNodeNext(pSecondNode) == nullptr);
        CHECK(pSecondNode->GetPrevNode() == pFirstNode);
        CHECK(pSecondNode->GetSize() == 128 - sizeof(Data16B) - 2 * nodeHeaderSize);
    }

    // block1: Node(16 + 16) + Node(16 + 24) + NodeFree(16 + 40)
    Data24B* pBlock1Data24_1 = CUSTOM_NEW<Data24B>();

    {
        CHECK(gAllocator->GetCurrentBlockNum() == 1);

        auto pFirstBlock = gAllocator->GetFirstBlockPtr();
        auto pFirstNode = gAllocator->GetBlockFirstNodePtr(pFirstBlock);

        CHECK(pFirstNode->Used() == true);
        CHECK(pFirstNode->GetSize() == sizeof(Data16B));

        auto pSecondNode = gAllocator->GetNodeNext(pFirstNode);

        CHECK(pSecondNode->Used() == true);
        CHECK(pSecondNode->GetPrevNode() == pFirstNode);
        CHECK(pSecondNode->GetSize() == sizeof(Data24B));

        auto pThirdNode = gAllocator->GetNodeNext(pSecondNode);

        CHECK(pThirdNode->Used() == false);
        CHECK(gAllocator->GetNodeNext(pThirdNode) == nullptr);
        CHECK(pThirdNode->GetPrevNode() == pSecondNode);
        CHECK(pThirdNode->GetSize() == 128 - sizeof(Data16B) - sizeof(Data24B) - 3 * nodeHeaderSize);
    }

    // block1: Node(16 + 16) + Node(16 + 24) + Node(16 + 32 + 8)
    Data32B* pBlock2Data32_1 = CUSTOM_NEW<Data32B>();

    {
        CHECK(gAllocator->GetCurrentBlockNum() == 1);

        auto pFirstBlock = gAllocator->GetFirstBlockPtr();
        auto pFirstNode = gAllocator->GetBlockFirstNodePtr(pFirstBlock);

        CHECK(pFirstNode->Used() == true);
        CHECK(pFirstNode->GetSize() == sizeof(Data16B));

        auto pSecondNode = gAllocator->GetNodeNext(pFirstNode);

        CHECK(pSecondNode->Used() == true);
        CHECK(pSecondNode->GetPrevNode() == pFirstNode);
        CHECK(pSecondNode->GetSize() == sizeof(Data24B));

        auto pThirdNode = gAllocator->GetNodeNext(pSecondNode);

        CHECK(pThirdNode->Used() == true);
        CHECK(gAllocator->GetNodeNext(pThirdNode) == nullptr);
        CHECK(pThirdNode->GetPrevNode() == pSecondNode);
        CHECK(pThirdNode->GetSize() == 128 - sizeof(Data16B) - sizeof(Data24B) - 3 * nodeHeaderSize);
    }

    // block1: Node(16 + 16) + Node(16 + 24) + Node(16 + 32 + 8)
    // block2: Node(16 + 64) + NodeFree(16 + 32)
    Data64B* pBlock3Data64_1 = CUSTOM_NEW<Data64B>();

    {
        CHECK(gAllocator->GetCurrentBlockNum() == 2);

        auto pFirstBlock = gAllocator->GetFirstBlockPtr();
        auto pSecondBlock = pFirstBlock->pNext;

        auto pFirstNode = gAllocator->GetBlockFirstNodePtr(pSecondBlock);

        CHECK(pFirstNode->Used() == true);
        CHECK(pFirstNode->GetSize() == sizeof(Data64B));

        auto pSecondNode = gAllocator->GetNodeNext(pFirstNode);

        CHECK(pSecondNode->Used() == false);
        CHECK(gAllocator->GetNodeNext(pSecondNode) == nullptr);
        CHECK(pSecondNode->GetPrevNode() == pFirstNode);
        CHECK(pSecondNode->GetSize() == 128 - sizeof(Data64B) - 2 * nodeHeaderSize);
    }

    // block1: Node(16 + 16) + Node(16 + 24) + Node(16 + 32 + 8)
    // block2: Node(16 + 64) + NodeFree(16 + 32)
    // block3: Node(16 + 64) + NodeFree(16 + 32)
    Data64B* pBlock3Data64_2 = CUSTOM_NEW<Data64B>();

    {
        CHECK(gAllocator->GetCurrentBlockNum() == 3);

        auto pFirstBlock = gAllocator->GetFirstBlockPtr();
        auto pThirdBlock = pFirstBlock->pNext->pNext;

        auto pFirstNode = gAllocator->GetBlockFirstNodePtr(pThirdBlock);

        CHECK(pFirstNode->Used() == true);
        CHECK(pFirstNode->GetSize() == sizeof(Data64B));

        auto pSecondNode = gAllocator->GetNodeNext(pFirstNode);

        CHECK(pSecondNode->Used() == false);
        CHECK(gAllocator->GetNodeNext(pSecondNode) == nullptr);
        CHECK(pSecondNode->GetPrevNode() == pFirstNode);
        CHECK(pSecondNode->GetSize() == 128 - sizeof(Data64B) - 2 * nodeHeaderSize);
    }

    // block1: Node(16 + 16) + NodeFree(16 + 24) + Node(16 + 32 + 8)
    // block2: Node(16 + 64) + NodeFree(16 + 32)
    // block3: Node(16 + 64) + NodeFree(16 + 32)
    CUSTOM_DELETE(pBlock1Data24_1);

    {
        CHECK(gAllocator->GetCurrentBlockNum() == 3);

        auto pFirstBlock = gAllocator->GetFirstBlockPtr();
        auto pFirstNode = gAllocator->GetBlockFirstNodePtr(pFirstBlock);

        CHECK(pFirstNode->Used() == true);
        CHECK(pFirstNode->GetSize() == sizeof(Data16B));

        auto pSecondNode = gAllocator->GetNodeNext(pFirstNode);

        CHECK(pSecondNode->Used() == false);
        CHECK(pSecondNode->GetPrevNode() == pFirstNode);
        CHECK(pSecondNode->GetSize() == sizeof(Data24B));

        auto pThirdNode = gAllocator->GetNodeNext(pSecondNode);

        CHECK(pThirdNode->Used() == true);
        CHECK(gAllocator->GetNodeNext(pThirdNode) == nullptr);
        CHECK(pThirdNode->GetPrevNode() == pSecondNode);
        CHECK(pThirdNode->GetSize() == 128 - sizeof(Data16B) - sizeof(Data24B) - 3 * nodeHeaderSize);
    }

    // block1: Node(16 + 16) + NodeFree(16 + 24) + Node(16 + 32 + 8)
    // block3: Node(16 + 64) + NodeFree(16 + 32)
    CUSTOM_DELETE(pBlock3Data64_1);

    {
        CHECK(gAllocator->GetCurrentBlockNum() == 2);
    }

    // block1: Node(16 + 16) + NodeFree(16 + 80)
    // block3: Node(16 + 64) + NodeFree(16 + 32)
    CUSTOM_DELETE(pBlock2Data32_1);

    {
        CHECK(gAllocator->GetCurrentBlockNum() == 2);

        auto pFirstBlock = gAllocator->GetFirstBlockPtr();
        auto pFirstNode = gAllocator->GetBlockFirstNodePtr(pFirstBlock);

        CHECK(pFirstNode->Used() == true);
        CHECK(pFirstNode->GetSize() == sizeof(Data16B));

        auto pSecondNode = gAllocator->GetNodeNext(pFirstNode);

        CHECK(pSecondNode->Used() == false);
        CHECK(gAllocator->GetNodeNext(pSecondNode) == nullptr);
        CHECK(pSecondNode->GetPrevNode() == pFirstNode);
        CHECK(pSecondNode->GetSize() == 128 - sizeof(Data16B) - 2 * nodeHeaderSize);
    }

    // block1: Node(16 + 16) + NodeFree(16 + 80)
    CUSTOM_DELETE(pBlock3Data64_2);

    {
        CHECK(gAllocator->GetCurrentBlockNum() == 1);
    }

    // block1: NodeFree(16 + 112)
    CUSTOM_DELETE(pBlock1Data16_1);

    {
        CHECK(gAllocator->GetCurrentBlockNum() == 1);

        auto pFirstBlock = gAllocator->GetFirstBlockPtr();
        auto pFirstNode = gAllocator->GetBlockFirstNodePtr(pFirstBlock);

        CHECK(pFirstNode->Used() == false);
        CHECK(gAllocator->GetNodeNext(pFirstNode) == nullptr);
        CHECK(pFirstNode->GetSize() == 128 - nodeHeaderSize);
    }
}

