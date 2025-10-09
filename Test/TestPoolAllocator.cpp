#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include <new>
#include <random>
#include <algorithm>
#include "EAllocKit/PoolAllocator.hpp"
#include "Helper.h"

using namespace EAllocKit;

namespace Alloc
{
    template<typename T>
    T* New(PoolAllocator* pAllocator)
    {
        void* pMem = pAllocator->Allocate();
        if (pMem == nullptr)
            return nullptr;

        return new (AllocatorMarker(), pMem) T();
    }

    template<typename T, typename... Args>
    T* New(PoolAllocator* pAllocator, Args&&... args)
    {
        void* pMem = pAllocator->Allocate();
        if (pMem == nullptr)
            return nullptr;

        return new (AllocatorMarker(), pMem) T(std::forward<Args>(args)...);
    }
}

template<typename T, size_t alignment, size_t num>
void AllocateAndDelete()
{
    PoolAllocator allocator(sizeof(T), num, alignment);

    CHECK(allocator.GetAvailableBlockCount() == num);

    std::vector<T*> dataVec;
    for (size_t i = 0; i < num; i++)
    {
        T* pData = Alloc::New<T>(&allocator);
        dataVec.push_back(pData);
    }

    CHECK(allocator.GetAvailableBlockCount() == 0);
    CHECK(allocator.GetFreeListHeadNode() == nullptr);

    std::random_device rd;
    std::mt19937 g(rd());

    std::shuffle(dataVec.begin(), dataVec.end(), g);

    for (size_t i = 0; i < num; i++)
        Alloc::Delete(&allocator, dataVec[i]);

    CHECK(allocator.GetAvailableBlockCount() == num);
}

TEST_CASE("PoolAllocator - Basic Allocation")
{
    AllocateAndDelete<uint32_t, 4, 128>();
    AllocateAndDelete<uint32_t, 4, 256>();
    AllocateAndDelete<uint32_t, 8, 4096>();
    AllocateAndDelete<Data64B, 8, 1024>();
    AllocateAndDelete<Data128B, 8, 4096>();
}

TEST_CASE("PoolAllocator - Pool Exhaustion")
{
    SUBCASE("Allocate until exhausted")
    {
        PoolAllocator allocator(sizeof(Data64B), 10, 8);
        
        std::vector<Data64B*> ptrs;
        for (int i = 0; i < 10; i++)
        {
            auto* p = Alloc::New<Data64B>(&allocator);
            CHECK(p != nullptr);
            CHECK(allocator.GetAvailableBlockCount() == 10 - i - 1);
            ptrs.push_back(p);
        }
        
        CHECK(allocator.GetAvailableBlockCount() == 0);
        
        // Try to allocate when pool is full
        auto* p = Alloc::New<Data64B>(&allocator);
        CHECK(p == nullptr);
        
        // Free one and retry
        Alloc::Delete(&allocator, ptrs[0]);
        CHECK(allocator.GetAvailableBlockCount() == 1);
        
        auto* p2 = Alloc::New<Data64B>(&allocator);
        CHECK(p2 != nullptr);
        CHECK(allocator.GetAvailableBlockCount() == 0);
        
        // Cleanup
        ptrs[0] = p2;
        for (auto* ptr : ptrs)
        {
            Alloc::Delete(&allocator, ptr);
        }
    }
    
    SUBCASE("Multiple allocate-free cycles")
    {
        PoolAllocator allocator(sizeof(uint32_t), 50, 8);
        
        for (int cycle = 0; cycle < 5; cycle++)
        {
            std::vector<uint32_t*> ptrs;
            
            for (int i = 0; i < 50; i++)
            {
                auto* p = Alloc::New<uint32_t>(&allocator);
                CHECK(p != nullptr);
                ptrs.push_back(p);
            }
            
            CHECK(allocator.GetAvailableBlockCount() == 0);
            
            for (auto* p : ptrs)
            {
                Alloc::Delete(&allocator, p);
            }
            
            CHECK(allocator.GetAvailableBlockCount() == 50);
        }
    }
}

TEST_CASE("PoolAllocator - Block Reuse")
{
    SUBCASE("Verify block reuse")
    {
        PoolAllocator allocator(sizeof(Data64B), 5, 8);
        
        auto* p1 = Alloc::New<Data64B>(&allocator);
        void* addr1 = p1;
        
        Alloc::Delete(&allocator, p1);
        
        auto* p2 = Alloc::New<Data64B>(&allocator);
        CHECK(p2 == addr1); // Should reuse same block
        
        Alloc::Delete(&allocator, p2);
    }
    
    SUBCASE("LIFO reuse pattern")
    {
        PoolAllocator allocator(sizeof(uint32_t), 10, 8);
        
        std::vector<uint32_t*> ptrs;
        for (int i = 0; i < 5; i++)
        {
            auto* p = Alloc::New<uint32_t>(&allocator);
            ptrs.push_back(p);
        }
        
        // Save addresses before freeing
        std::vector<void*> addresses;
        for (int i = 0; i < 5; i++)
        {
            addresses.push_back(ptrs[i]);
        }
        
        // Free in reverse order: [4,3,2,1,0]
        // This creates free list: 0 -> 1 -> 2 -> 3 -> 4 -> nullptr
        for (int i = 4; i >= 0; i--)
        {
            Alloc::Delete(&allocator, ptrs[i]);
        }
        
        // Reallocate - should get blocks in LIFO order: 0,1,2,3,4
        std::vector<uint32_t*> newPtrs;
        for (int i = 0; i < 5; i++)
        {
            auto* p = Alloc::New<uint32_t>(&allocator);
            CHECK(p == addresses[i]); // LIFO: get addresses[0,1,2,3,4]
            newPtrs.push_back(p);
        }
        
        // Cleanup
        for (auto* p : newPtrs)
        {
            Alloc::Delete(&allocator, p);
        }
    }
}

TEST_CASE("PoolAllocator - Random Access Pattern")
{
    SUBCASE("Random allocation and deallocation")
    {
        PoolAllocator allocator(sizeof(Data64B), 100, 8);
        
        std::vector<Data64B*> active;
        std::random_device rd;
        std::mt19937 gen(rd());
        
        for (int i = 0; i < 200; i++)
        {
            if (active.size() < 50 || (active.size() < 100 && gen() % 2 == 0))
            {
                // Allocate
                auto* p = Alloc::New<Data64B>(&allocator);
                if (p) active.push_back(p);
            }
            else if (!active.empty())
            {
                // Deallocate random element
                size_t idx = gen() % active.size();
                Alloc::Delete(&allocator, active[idx]);
                active.erase(active.begin() + idx);
            }
        }
        
        // Cleanup
        for (auto* p : active)
        {
            Alloc::Delete(&allocator, p);
        }
        
        CHECK(allocator.GetAvailableBlockCount() == 100);
    }
}

TEST_CASE("PoolAllocator - Edge Cases")
{
    SUBCASE("Single block pool")
    {
        PoolAllocator allocator(sizeof(uint32_t), 1, 8);
        
        CHECK(allocator.GetAvailableBlockCount() == 1);
        
        auto* p1 = Alloc::New<uint32_t>(&allocator);
        CHECK(p1 != nullptr);
        CHECK(allocator.GetAvailableBlockCount() == 0);
        
        auto* p2 = Alloc::New<uint32_t>(&allocator);
        CHECK(p2 == nullptr);
        
        Alloc::Delete(&allocator, p1);
        CHECK(allocator.GetAvailableBlockCount() == 1);
    }
    
    SUBCASE("Large pool")
    {
        PoolAllocator allocator(sizeof(uint32_t), 10000, 8);
        
        CHECK(allocator.GetAvailableBlockCount() == 10000);
        
        std::vector<uint32_t*> ptrs;
        for (int i = 0; i < 1000; i++)
        {
            auto* p = Alloc::New<uint32_t>(&allocator);
            CHECK(p != nullptr);
            ptrs.push_back(p);
        }
        
        CHECK(allocator.GetAvailableBlockCount() == 9000);
        
        for (auto* p : ptrs)
        {
            Alloc::Delete(&allocator, p);
        }
    }
    
    SUBCASE("Double free safety")
    {
        PoolAllocator allocator(sizeof(uint32_t), 10, 8);
        
        auto* p = Alloc::New<uint32_t>(&allocator);
        CHECK(p != nullptr);
        
        size_t before = allocator.GetAvailableBlockCount();
        Alloc::Delete(&allocator, p);
        size_t after = allocator.GetAvailableBlockCount();
        
        CHECK(after == before + 1);
        
        // Second delete - behavior depends on implementation
        // but shouldn't crash
        Alloc::Delete(&allocator, p);
    }
    
    SUBCASE("Null pointer delete")
    {
        PoolAllocator allocator(sizeof(uint32_t), 10, 8);
        
        // Should handle null gracefully
        Alloc::Delete<uint32_t>(&allocator, nullptr);
    }
}

TEST_CASE("PoolAllocator - Data Integrity")
{
    SUBCASE("Write and read data")
    {
        PoolAllocator allocator(sizeof(uint32_t), 100, 8);
        
        std::vector<uint32_t*> ptrs;
        for (uint32_t i = 0; i < 50; i++)
        {
            auto* p = Alloc::New<uint32_t>(&allocator);
            CHECK(p != nullptr);
            *p = i * 100;
            CHECK(*p == i * 100);
            ptrs.push_back(p);
        }
        
        // Verify all values
        for (size_t i = 0; i < ptrs.size(); i++)
        {
            CHECK(*ptrs[i] == i * 100);
        }
        
        // Modify and verify again
        for (size_t i = 0; i < ptrs.size(); i++)
        {
            *ptrs[i] = i * 200;
        }
        
        for (size_t i = 0; i < ptrs.size(); i++)
        {
            CHECK(*ptrs[i] == i * 200);
        }
        
        // Cleanup
        for (auto* p : ptrs)
        {
            Alloc::Delete(&allocator, p);
        }
    }
    
    SUBCASE("Complex type allocation")
    {
        PoolAllocator allocator(sizeof(Data128B), 20, 8);
        
        std::vector<Data128B*> ptrs;
        for (int i = 0; i < 20; i++)
        {
            auto* p = Alloc::New<Data128B>(&allocator);
            CHECK(p != nullptr);
            
            // Initialize data - Data128B has 128 bytes
            for (int j = 0; j < 128; j++)
            {
                p->data[j] = static_cast<uint8_t>((i * 128 + j) % 256);
            }
            
            ptrs.push_back(p);
        }
        
        // Verify data integrity
        for (size_t i = 0; i < ptrs.size(); i++)
        {
            for (int j = 0; j < 128; j++)
            {
                CHECK(ptrs[i]->data[j] == static_cast<uint8_t>((i * 128 + j) % 256));
            }
        }
        
        // Cleanup
        for (auto* p : ptrs)
        {
            Alloc::Delete(&allocator, p);
        }
    }
}

TEST_CASE("PoolAllocator - Alignment Verification")
{
    SUBCASE("Check alignment for all allocations")
    {
        PoolAllocator allocator(sizeof(uint64_t), 100, 8);
        
        std::vector<uint64_t*> ptrs;
        for (int i = 0; i < 100; i++)
        {
            auto* p = Alloc::New<uint64_t>(&allocator);
            CHECK(p != nullptr);
            CHECK(reinterpret_cast<size_t>(p) % 8 == 0);
            ptrs.push_back(p);
        }
        
        for (auto* p : ptrs)
        {
            Alloc::Delete(&allocator, p);
        }
    }
    
    SUBCASE("Different alignments")
    {
        {
            PoolAllocator allocator(sizeof(uint32_t), 10, 4);
            auto* p = Alloc::New<uint32_t>(&allocator);
            CHECK(reinterpret_cast<size_t>(p) % 4 == 0);
            Alloc::Delete(&allocator, p);
        }
        
        {
            PoolAllocator allocator(sizeof(Data128B), 10, 16);
            auto* p = Alloc::New<Data128B>(&allocator);
            CHECK(reinterpret_cast<size_t>(p) % 16 == 0);
            Alloc::Delete(&allocator, p);
        }
    }
}
