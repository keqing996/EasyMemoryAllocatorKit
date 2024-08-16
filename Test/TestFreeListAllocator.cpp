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
    CHECK(pFirstNode->used == true);

    auto pSecondNode = pFirstNode->pNext;
    PrintPtrAddr("Second node addr:", pSecondNode);

    CHECK(ToAddr(pSecondNode) == ToAddr(pUint) + Util::UpAlignment(sizeof(uint32_t), alignment));

    uint32_t* pUint2 = CUSTOM_NEW<uint32_t>();
    *pUint2 = 0xABCDABCD;

    CHECK(pFirstNode->used == true);

    auto pThirdNode = pSecondNode->pNext;
    PrintPtrAddr("Third node addr:", pThirdNode);

    CUSTOM_DELETE(pUint);

    CHECK(pFirstNode->used == false);

    pUint = CUSTOM_NEW<uint32_t>();

    CHECK(ToAddr(pFirstNodeStart) == ToAddr(pUint));
    CHECK(pFirstNode->used == true);
    CHECK(pFirstNode->pNext == pSecondNode);

    uint32_t* pUint3 = CUSTOM_NEW<uint32_t>();
    *pUint3 = 0xABCDABCD;

    CUSTOM_DELETE(pUint);
    CUSTOM_DELETE(pUint2);

    CHECK(pFirstNode->used == false);
    CHECK(pFirstNode->pNext == pThirdNode);
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

    // block1: Node(24B) -> Data(40B) -> Padding(64B)
    Temp* pTemp1 = CUSTOM_NEW<Temp>();

    // block1: Node(24B) -> Data(40B) -> Node(24B) -> Data(40B)
    Temp* pTemp2 = CUSTOM_NEW<Temp>();
    CHECK(gAllocator->GetCurrentBlockNum() == 1);

    // block1: Node(24B) -> Data(40B) -> Node(24B) -> Data(40B)
    // block2: Node(24B) -> Data(40B) -> Padding(64B)
    Temp* pTemp3 = CUSTOM_NEW<Temp>();

    // block1: Node(24B) -> Data(40B) -> Node(24B) -> Data(40B)
    // block2: Node(24B) -> Data(40B) -> Node(24B) -> Data(40B)
    Temp* pTemp4 = CUSTOM_NEW<Temp>();

    CHECK(gAllocator->GetCurrentBlockNum() == 2);

    // block1: Node(24B) -> Data(40B) -> Node(24B) -> Data(40B)
    // block2: Node(24B) -> Data(40B) -> Node(24B) -> Data(40B)
    // block3: Node(24B) -> Data(40B) -> Padding(64B)
    Temp* pTemp5 = CUSTOM_NEW<Temp>();

    CHECK(gAllocator->GetCurrentBlockNum() == 3);

    CUSTOM_DELETE(pTemp1);
    CUSTOM_DELETE(pTemp2);

    CHECK(gAllocator->GetCurrentBlockNum() == 3);

    CUSTOM_DELETE(pTemp3);
    CUSTOM_DELETE(pTemp4);

    CHECK(gAllocator->GetCurrentBlockNum() == 2);

    CUSTOM_DELETE(pTemp5);

    CHECK(gAllocator->GetCurrentBlockNum() == 1);
}

TEST_CASE("TestSplitAndMerge")
{
    printf("======= Test Split & Merge =======\n");

    size_t alignment = 8;
    AllocatorScope scope(256, alignment);

    size_t nodeHeaderSize = Util::GetPaddedSize<FreeListAllocator::NodeHeader>(alignment);

    struct Data16B { uint8_t data[16]; };

    // block1: Node(24B) -> Data(16B) -> Node(24B) -> Padding(192B)
    Data16B* pTestData1 = CUSTOM_NEW<Data16B>();

    // block1: Node(24B) -> Data(16B) -> Node(24B) -> Data(16B) -> Node(24B) -> Padding(152B)
    Data16B* pTestData2 = CUSTOM_NEW<Data16B>();

    // block1: Node(24B) -> Data(16B) -> Node(24B) -> Data(16B) -> Node(24B) -> Data(16B) -> Node(24B) -> Padding(112B)
    Data16B* pTestData3 = CUSTOM_NEW<Data16B>();

    // block1: Node(24B) -> Data(16B) -> Node(24B) -> Data(16B) -> Node(24B) -> Data(16B) -> Node(24B)
    //       -> Data(16B) -> Node(24B) -> Padding(72B)
    Data16B* pTestData4 = CUSTOM_NEW<Data16B>();

    // block1: Node(24B) -> Data(16B) -> Node(24B) -> Data(16B) -> Node(24B) -> Data(16B) -> Node(24B)
    //       -> Data(16B) -> Node(24B) -> Data(16B) -> Node(24B) -> Padding(32B)
    Data16B* pTestData5 = CUSTOM_NEW<Data16B>();

    // block1: Node(24B) -> Data(16B) -> Node(24B) -> Data(16B) -> Node(24B) -> Free(16B) -> Node(24B)
    //       -> Data(16B) -> Node(24B) -> Data(16B) -> Node(24B) -> Padding(32B)
    CUSTOM_DELETE(pTestData3);

    // block1: Node(24B) -> Data(16B) -> Node(24B) -> Free(56B) -> Node(24B) -> Data(16B) -> Node(24B)
    //       -> Data(16B) -> Node(24B) -> Padding(32B)
    CUSTOM_DELETE(pTestData2);

    // block1: Node(24B) -> Data(16B) -> Node(24B) -> Free(96B)  -> Node(24B) -> Data(16B) -> Node(24B) -> Padding(32B)
    CUSTOM_DELETE(pTestData4);

    auto pFirstBlock = gAllocator->GetFirstBlockPtr();
    auto pFirstNode = gAllocator->GetBlockFirstNodePtr(pFirstBlock);

    CHECK(pFirstNode->used == true);
    CHECK(pFirstNode->pPrev == nullptr);

    auto pSecondNode = pFirstNode->pNext;
    CHECK(pSecondNode->used == false);
    CHECK(pSecondNode->pPrev == pFirstNode);

    auto pThirdNode = pSecondNode->pNext;
    CHECK(pThirdNode->used == true);
    CHECK(pThirdNode->pPrev == pSecondNode);

    auto pFourthNode = pThirdNode->pNext;
    CHECK(pFourthNode->used == false);
    CHECK(pFourthNode->pPrev == pThirdNode);
    CHECK(pFourthNode->pNext == nullptr);

    size_t freeGapSize = 2 * nodeHeaderSize + 3 * sizeof(Data16B);

    CHECK(ToAddr(pFirstNode) + nodeHeaderSize + sizeof(Data16B) == ToAddr(pSecondNode));
    CHECK(ToAddr(pSecondNode) + nodeHeaderSize + freeGapSize == ToAddr(pThirdNode));
    CHECK(ToAddr(pThirdNode) + nodeHeaderSize + sizeof(Data16B) == ToAddr(pFourthNode));

    CUSTOM_DELETE(pTestData1);
    CUSTOM_DELETE(pTestData5);
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

    // block1: Node(24B) -> Data(16B) -> Node(24B) -> Padding(64B)
    Data16B* pBlock1Data16_1 = CUSTOM_NEW<Data16B>();

    {
        CHECK(gAllocator->GetCurrentBlockNum() == 1);

        auto pFirstBlock = gAllocator->GetFirstBlockPtr();
        auto pFirstNode = gAllocator->GetBlockFirstNodePtr(pFirstBlock);

        CHECK(pFirstNode->used == true);

        auto pSecondNode = pFirstNode->pNext;

        CHECK(pSecondNode->used == false);
        CHECK(pSecondNode->pNext == nullptr);
        CHECK(pSecondNode->pPrev == pFirstNode);
        CHECK(ToAddr(pFirstNode) + nodeHeaderSize + sizeof(Data16B) == ToAddr(pSecondNode));
    }

    // block1: Node(24B) -> Data(16B) -> Node(24B) -> Data(24B) -> Node(24B) -> Padding(16B)
    Data24B* pBlock1Data24_1 = CUSTOM_NEW<Data24B>();

    {
        CHECK(gAllocator->GetCurrentBlockNum() == 1);

        auto pFirstBlock = gAllocator->GetFirstBlockPtr();
        auto pFirstNode = gAllocator->GetBlockFirstNodePtr(pFirstBlock);

        CHECK(pFirstNode->used == true);

        auto pSecondNode = pFirstNode->pNext;

        CHECK(pSecondNode->used == true);
        CHECK(pSecondNode->pPrev == pFirstNode);
        CHECK(ToAddr(pFirstNode) + nodeHeaderSize + sizeof(Data16B) == ToAddr(pSecondNode));

        auto pThirdNode = pSecondNode->pNext;

        CHECK(pThirdNode->used == false);
        CHECK(pThirdNode->pNext == nullptr);
        CHECK(pThirdNode->pPrev == pSecondNode);
        CHECK(ToAddr(pSecondNode) + nodeHeaderSize + sizeof(Data24B) == ToAddr(pThirdNode));
    }

    // block1: Node(24B) -> Data(16B) -> Node(24B) -> Data(24B) -> Node(24B) -> Padding(16B)
    // block2: Node(24B) -> Data(32B) -> Node(24B) -> Padding(48B)
    Data32B* pBlock2Data32_1 = CUSTOM_NEW<Data32B>();

    {
        CHECK(gAllocator->GetCurrentBlockNum() == 2);

        auto pFirstBlock = gAllocator->GetFirstBlockPtr();
        auto pSecondBlock = pFirstBlock->pNext;

        auto pFirstNode = gAllocator->GetBlockFirstNodePtr(pSecondBlock);

        CHECK(pFirstNode->used == true);

        auto pSecondNode = pFirstNode->pNext;

        CHECK(pSecondNode->used == false);
        CHECK(pSecondNode->pNext == nullptr);
        CHECK(pSecondNode->pPrev == pFirstNode);
        CHECK(ToAddr(pFirstNode) + nodeHeaderSize + sizeof(Data32B) == ToAddr(pSecondNode));
    }

    // block1: Node(24B) -> Data(16B) -> Node(24B) -> Data(24B) -> Node(24B) -> Padding(16B)
    // block2: Node(24B) -> Data(32B) -> Node(24B) -> Padding(48B)
    // block3: Node(24B) -> Data(64B) -> Node(24B) -> Padding(16B)
    Data64B* pBlock3Data64_1 = CUSTOM_NEW<Data64B>();

    {
        CHECK(gAllocator->GetCurrentBlockNum() == 3);
    }


    // block1: Node(24B) -> Data(16B) -> Node(24B) -> Padding(64B)
    // block2: Node(24B) -> Data(32B) -> Node(24B) -> Padding(48B)
    // block3: Node(24B) -> Data(64B) -> Node(24B) -> Padding(16B)
    CUSTOM_DELETE(pBlock1Data24_1);

    {
        CHECK(gAllocator->GetCurrentBlockNum() == 3);

        auto pFirstBlock = gAllocator->GetFirstBlockPtr();
        auto pFirstNode = gAllocator->GetBlockFirstNodePtr(pFirstBlock);

        CHECK(pFirstNode->used == true);

        auto pSecondNode = pFirstNode->pNext;

        CHECK(pSecondNode->used == false);
        CHECK(pSecondNode->pNext == nullptr);
        CHECK(pSecondNode->pPrev == pFirstNode);
    }

    // block1: Node(24B) -> Data(16B) -> Node(24B) -> Padding(64B)
    // block3: Node(24B) -> Data(64B) -> Node(24B) -> Padding(16B)
    CUSTOM_DELETE(pBlock2Data32_1);

    {
        CHECK(gAllocator->GetCurrentBlockNum() == 2);
    }

    // block1: Node(24B) -> Padding(104B)
    // block3: Node(24B) -> Data(64B) -> Node(24B) -> Padding(16B)
    CUSTOM_DELETE(pBlock1Data16_1);

    {
        CHECK(gAllocator->GetCurrentBlockNum() == 2);

        auto pFirstBlock = gAllocator->GetFirstBlockPtr();
        auto pFirstNode = gAllocator->GetBlockFirstNodePtr(pFirstBlock);

        CHECK(pFirstNode->used == false);
        CHECK(pFirstNode->pNext == nullptr);
    }

    // block1: Node(24B) -> Padding(104B)
    CUSTOM_DELETE(pBlock3Data64_1);

    {
        CHECK(gAllocator->GetCurrentBlockNum() == 1);
    }
}

