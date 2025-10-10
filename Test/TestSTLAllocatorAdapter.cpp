#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include <vector>
#include <list>
#include <string>
#include "EAllocKit/FreeListAllocator.hpp"
#include "EAllocKit/LinearAllocator.hpp"
#include "EAllocKit/StackAllocator.hpp"
#include "EAllocKit/PoolAllocator.hpp"
#include "EAllocKit/TLSFAllocator.hpp"
#include "EAllocKit/BuddyAllocator.hpp"
#include "EAllocKit/FrameAllocator.hpp"
// Note: SlabAllocator is not included in STL adapter tests because it requires
// fixed-size objects at compile time, which conflicts with STL container requirements
// Note: ArenaAllocator is not compatible with STL containers due to explicit arena management
#include "EAllocKit/Util/STLAllocatorAdapter.hpp"

using namespace EAllocKit;

// Test struct for verification
struct TestObject
{
    int value;
    double data;
    
    TestObject() : value(0), data(0.0) {}
    TestObject(int v, double d) : value(v), data(d) {}
    
    bool operator==(const TestObject& other) const
    {
        return value == other.value && data == other.data;
    }
};

// ========================================
// FreeListAllocator Tests
// ========================================

TEST_CASE("STLAllocatorAdapter - FreeListAllocator with std::vector<int>")
{
    constexpr size_t allocatorSize = 4096;
    FreeListAllocator allocator(allocatorSize);
    
    STLAllocatorAdapter<int, FreeListAllocator> adapter(&allocator);
    std::vector<int, STLAllocatorAdapter<int, FreeListAllocator>> vec(adapter);
    
    // Test push_back
    for (int i = 0; i < 10; ++i)
    {
        vec.push_back(i);
    }
    
    CHECK(vec.size() == 10);
    
    // Verify values
    for (int i = 0; i < 10; ++i)
    {
        CHECK(vec[i] == i);
    }
    
    // Test erase
    vec.erase(vec.begin() + 5);
    CHECK(vec.size() == 9);
    CHECK(vec[5] == 6);
}

TEST_CASE("STLAllocatorAdapter - FreeListAllocator with std::vector<TestObject>")
{
    constexpr size_t allocatorSize = 8192;
    FreeListAllocator allocator(allocatorSize);
    
    STLAllocatorAdapter<TestObject, FreeListAllocator> adapter(&allocator);
    std::vector<TestObject, STLAllocatorAdapter<TestObject, FreeListAllocator>> vec(adapter);
    
    vec.push_back(TestObject(1, 1.5));
    vec.push_back(TestObject(2, 2.5));
    vec.push_back(TestObject(3, 3.5));
    
    CHECK(vec.size() == 3);
    CHECK(vec[0] == TestObject(1, 1.5));
    CHECK(vec[1] == TestObject(2, 2.5));
    CHECK(vec[2] == TestObject(3, 3.5));
}

TEST_CASE("STLAllocatorAdapter - FreeListAllocator with std::list<int>")
{
    constexpr size_t allocatorSize = 4096;
    FreeListAllocator allocator(allocatorSize);
    
    STLAllocatorAdapter<int, FreeListAllocator> adapter(&allocator);
    std::list<int, STLAllocatorAdapter<int, FreeListAllocator>> list(adapter);
    
    for (int i = 0; i < 10; ++i)
    {
        list.push_back(i);
    }
    
    CHECK(list.size() == 10);
    
    int expected = 0;
    for (int val : list)
    {
        CHECK(val == expected++);
    }
}

// ========================================
// LinearAllocator Tests
// ========================================

TEST_CASE("STLAllocatorAdapter - LinearAllocator with std::vector<int>")
{
    constexpr size_t allocatorSize = 4096;
    LinearAllocator allocator(allocatorSize);
    
    STLAllocatorAdapter<int, LinearAllocator> adapter(&allocator);
    std::vector<int, STLAllocatorAdapter<int, LinearAllocator>> vec(adapter);
    
    // Note: LinearAllocator doesn't support deallocation, so operations that
    // trigger reallocation will work, but memory won't be reclaimed until Reset()
    for (int i = 0; i < 10; ++i)
    {
        vec.push_back(i);
    }
    
    CHECK(vec.size() == 10);
    
    for (int i = 0; i < 10; ++i)
    {
        CHECK(vec[i] == i);
    }
}

TEST_CASE("STLAllocatorAdapter - LinearAllocator with std::vector<double>")
{
    constexpr size_t allocatorSize = 4096;
    LinearAllocator allocator(allocatorSize);
    
    STLAllocatorAdapter<double, LinearAllocator> adapter(&allocator);
    std::vector<double, STLAllocatorAdapter<double, LinearAllocator>> vec(adapter);
    
    vec.push_back(1.1);
    vec.push_back(2.2);
    vec.push_back(3.3);
    
    CHECK(vec.size() == 3);
    CHECK(vec[0] == doctest::Approx(1.1));
    CHECK(vec[1] == doctest::Approx(2.2));
    CHECK(vec[2] == doctest::Approx(3.3));
}

// ========================================
// StackAllocator Tests
// ========================================

TEST_CASE("STLAllocatorAdapter - StackAllocator with std::vector<int>")
{
    constexpr size_t allocatorSize = 4096;
    StackAllocator allocator(allocatorSize);
    
    STLAllocatorAdapter<int, StackAllocator> adapter(&allocator);
    std::vector<int, STLAllocatorAdapter<int, StackAllocator>> vec(adapter);
    
    // Note: StackAllocator requires LIFO deallocation order
    // vector operations work but internal memory management follows LIFO
    for (int i = 0; i < 10; ++i)
    {
        vec.push_back(i);
    }
    
    CHECK(vec.size() == 10);
    
    for (int i = 0; i < 10; ++i)
    {
        CHECK(vec[i] == i);
    }
}

TEST_CASE("STLAllocatorAdapter - StackAllocator with std::list<int>")
{
    constexpr size_t allocatorSize = 4096;
    StackAllocator allocator(allocatorSize);
    
    STLAllocatorAdapter<int, StackAllocator> adapter(&allocator);
    std::list<int, STLAllocatorAdapter<int, StackAllocator>> list(adapter);
    
    for (int i = 0; i < 5; ++i)
    {
        list.push_back(i);
    }
    
    CHECK(list.size() == 5);
    
    // Verify LIFO behavior works with list
    list.pop_back();
    CHECK(list.size() == 4);
}

// ========================================
// PoolAllocator Tests
// ========================================

TEST_CASE("STLAllocatorAdapter - PoolAllocator with std::list<int>")
{
    // PoolAllocator is ideal for std::list since list allocates nodes one at a time
    // We need to account for the list node structure size
    constexpr size_t nodeSize = sizeof(int) + sizeof(void*) * 2; // approximate node size
    constexpr size_t blockCount = 20;
    PoolAllocator allocator(nodeSize, blockCount);
    
    STLAllocatorAdapter<int, PoolAllocator> adapter(&allocator);
    std::list<int, STLAllocatorAdapter<int, PoolAllocator>> list(adapter);
    
    // Add elements
    for (int i = 0; i < 10; ++i)
    {
        list.push_back(i);
    }
    
    CHECK(list.size() == 10);
    CHECK(allocator.GetAvailableBlockCount() >= 10);
    
    // Verify values
    int expected = 0;
    for (int val : list)
    {
        CHECK(val == expected++);
    }
    
    // Remove some elements
    list.pop_back();
    list.pop_back();
    CHECK(list.size() == 8);
}

TEST_CASE("STLAllocatorAdapter - PoolAllocator allocation limit")
{
    // Test that pool allocator correctly throws when trying to allocate more than 1 object at a time
    constexpr size_t nodeSize = sizeof(int);
    constexpr size_t blockCount = 10;
    PoolAllocator allocator(nodeSize, blockCount);
    
    STLAllocatorAdapter<int, PoolAllocator> adapter(&allocator);
    
    // This should work (allocating 1 object)
    int* ptr = adapter.allocate(1);
    CHECK(ptr != nullptr);
    
    // This should throw (allocating multiple objects at once)
    CHECK_THROWS_AS(adapter.allocate(2), std::bad_alloc);
    
    adapter.deallocate(ptr, 1);
}

// ========================================
// TLSFAllocator Tests
// ========================================

TEST_CASE("STLAllocatorAdapter - TLSFAllocator with std::vector<int>")
{
    constexpr size_t allocatorSize = 8192;
    TLSFAllocator<> allocator(allocatorSize);
    
    STLAllocatorAdapter<int, TLSFAllocator<>> adapter(&allocator);
    std::vector<int, STLAllocatorAdapter<int, TLSFAllocator<>>> vec(adapter);
    
    for (int i = 0; i < 20; ++i)
    {
        vec.push_back(i * 2);
    }
    
    CHECK(vec.size() == 20);
    
    for (int i = 0; i < 20; ++i)
    {
        CHECK(vec[i] == i * 2);
    }
}

TEST_CASE("STLAllocatorAdapter - TLSFAllocator with std::vector<TestObject>")
{
    constexpr size_t allocatorSize = 8192;
    TLSFAllocator<> allocator(allocatorSize);
    
    STLAllocatorAdapter<TestObject, TLSFAllocator<>> adapter(&allocator);
    std::vector<TestObject, STLAllocatorAdapter<TestObject, TLSFAllocator<>>> vec(adapter);
    
    vec.push_back(TestObject(10, 10.5));
    vec.push_back(TestObject(20, 20.5));
    vec.push_back(TestObject(30, 30.5));
    
    CHECK(vec.size() == 3);
    CHECK(vec[0] == TestObject(10, 10.5));
    CHECK(vec[1] == TestObject(20, 20.5));
    CHECK(vec[2] == TestObject(30, 30.5));
}

TEST_CASE("STLAllocatorAdapter - TLSFAllocator with std::list<std::string>")
{
    constexpr size_t allocatorSize = 16384;
    TLSFAllocator<> allocator(allocatorSize);
    
    // Note: std::string itself may use dynamic allocation internally
    // This tests that the list nodes are allocated from our allocator
    STLAllocatorAdapter<std::string, TLSFAllocator<>> adapter(&allocator);
    std::list<std::string, STLAllocatorAdapter<std::string, TLSFAllocator<>>> list(adapter);
    
    list.push_back("Hello");
    list.push_back("World");
    list.push_back("TLSF");
    list.push_back("Allocator");
    
    CHECK(list.size() == 4);
    
    auto it = list.begin();
    CHECK(*it++ == "Hello");
    CHECK(*it++ == "World");
    CHECK(*it++ == "TLSF");
    CHECK(*it++ == "Allocator");
}

// ========================================
// Mixed Container Tests
// ========================================

TEST_CASE("STLAllocatorAdapter - Multiple containers sharing FreeListAllocator")
{
    constexpr size_t allocatorSize = 16384;
    FreeListAllocator allocator(allocatorSize);
    
    STLAllocatorAdapter<int, FreeListAllocator> adapter(&allocator);
    std::vector<int, STLAllocatorAdapter<int, FreeListAllocator>> vec(adapter);
    std::list<int, STLAllocatorAdapter<int, FreeListAllocator>> list(adapter);
    
    // Use vector
    for (int i = 0; i < 5; ++i)
    {
        vec.push_back(i);
    }
    
    // Use list
    for (int i = 0; i < 5; ++i)
    {
        list.push_back(i * 10);
    }
    
    CHECK(vec.size() == 5);
    CHECK(list.size() == 5);
    
    // Verify vector
    for (int i = 0; i < 5; ++i)
    {
        CHECK(vec[i] == i);
    }
    
    // Verify list
    int expected = 0;
    for (int val : list)
    {
        CHECK(val == expected * 10);
        expected++;
    }
}

TEST_CASE("STLAllocatorAdapter - Adapter comparison operators")
{
    constexpr size_t allocatorSize = 4096;
    FreeListAllocator allocator1(allocatorSize);
    FreeListAllocator allocator2(allocatorSize);
    
    STLAllocatorAdapter<int, FreeListAllocator> adapter1(&allocator1);
    STLAllocatorAdapter<int, FreeListAllocator> adapter2(&allocator1);
    STLAllocatorAdapter<int, FreeListAllocator> adapter3(&allocator2);
    
    // Same allocator
    CHECK(adapter1 == adapter2);
    CHECK_FALSE(adapter1 != adapter2);
    
    // Different allocator
    CHECK_FALSE(adapter1 == adapter3);
    CHECK(adapter1 != adapter3);
}

TEST_CASE("STLAllocatorAdapter - Rebind functionality")
{
    constexpr size_t allocatorSize = 4096;
    FreeListAllocator allocator(allocatorSize);
    
    STLAllocatorAdapter<int, FreeListAllocator> intAdapter(&allocator);
    
    // Rebind to double
    using DoubleAdapter = STLAllocatorAdapter<int, FreeListAllocator>::rebind<double>::other;
    DoubleAdapter doubleAdapter(intAdapter);
    
    // Test that rebound adapter works
    double* ptr = doubleAdapter.allocate(1);
    CHECK(ptr != nullptr);
    
    doubleAdapter.construct(ptr, 3.14);
    CHECK(*ptr == doctest::Approx(3.14));
    
    doubleAdapter.destroy(ptr);
    doubleAdapter.deallocate(ptr, 1);
}

// ========================================
// Edge Cases and Error Handling
// ========================================

TEST_CASE("STLAllocatorAdapter - Allocate zero elements")
{
    FreeListAllocator allocator(1024, 8);
    STLAllocatorAdapter<int, FreeListAllocator> adapter(&allocator);
    
    int* ptr = adapter.allocate(0);
    CHECK(ptr == nullptr);
}

TEST_CASE("STLAllocatorAdapter - Large allocation")
{
    constexpr size_t allocatorSize = 1024 * 1024; // 1 MB
    TLSFAllocator<> allocator(allocatorSize);
    
    STLAllocatorAdapter<int, TLSFAllocator<>> adapter(&allocator);
    std::vector<int, STLAllocatorAdapter<int, TLSFAllocator<>>> vec(adapter);
    
    // Allocate a large number of elements
    for (int i = 0; i < 1000; ++i)
    {
        vec.push_back(i);
    }
    
    CHECK(vec.size() == 1000);
    
    // Verify some values
    CHECK(vec[0] == 0);
    CHECK(vec[500] == 500);
    CHECK(vec[999] == 999);
}

TEST_CASE("STLAllocatorAdapter - Construct and destroy with custom objects")
{
    FreeListAllocator allocator(4096, 8);
    STLAllocatorAdapter<TestObject, FreeListAllocator> adapter(&allocator);
    
    TestObject* ptr = adapter.allocate(1);
    CHECK(ptr != nullptr);
    
    adapter.construct(ptr, 42, 3.14);
    CHECK(ptr->value == 42);
    CHECK(ptr->data == doctest::Approx(3.14));
    
    adapter.destroy(ptr);
    adapter.deallocate(ptr, 1);
}

// ========================================
// BuddyAllocator Tests
// ========================================

TEST_CASE("STLAllocatorAdapter - BuddyAllocator with std::vector<int>")
{
    constexpr size_t allocatorSize = 8192;
    BuddyAllocator allocator(allocatorSize);
    
    STLAllocatorAdapter<int, BuddyAllocator> adapter(&allocator);
    std::vector<int, STLAllocatorAdapter<int, BuddyAllocator>> vec(adapter);
    
    for (int i = 0; i < 20; ++i)
    {
        vec.push_back(i * 2);
    }
    
    CHECK(vec.size() == 20);
    CHECK(vec[0] == 0);
    CHECK(vec[10] == 20);
    CHECK(vec[19] == 38);
}

TEST_CASE("STLAllocatorAdapter - BuddyAllocator with std::list<TestObject>")
{
    constexpr size_t allocatorSize = 16384;
    BuddyAllocator allocator(allocatorSize);
    
    STLAllocatorAdapter<TestObject, BuddyAllocator> adapter(&allocator);
    std::list<TestObject, STLAllocatorAdapter<TestObject, BuddyAllocator>> list(adapter);
    
    list.push_back(TestObject(10, 1.1));
    list.push_back(TestObject(20, 2.2));
    list.push_back(TestObject(30, 3.3));
    
    CHECK(list.size() == 3);
    
    auto it = list.begin();
    CHECK(it->value == 10);
    ++it;
    CHECK(it->value == 20);
    ++it;
    CHECK(it->value == 30);
}

// ========================================
// FrameAllocator Tests
// ========================================

TEST_CASE("STLAllocatorAdapter - FrameAllocator with std::vector<int>")
{
    constexpr size_t allocatorSize = 8192;
    FrameAllocator allocator(allocatorSize);
    
    STLAllocatorAdapter<int, FrameAllocator> adapter(&allocator);
    std::vector<int, STLAllocatorAdapter<int, FrameAllocator>> vec(adapter);
    
    for (int i = 0; i < 30; ++i)
    {
        vec.push_back(i);
    }
    
    CHECK(vec.size() == 30);
    CHECK(vec[15] == 15);
    
    // Frame allocator doesn't support individual deallocation
    // But STL containers will still work correctly
}

TEST_CASE("STLAllocatorAdapter - FrameAllocator frame reset")
{
    constexpr size_t allocatorSize = 4096;
    FrameAllocator allocator(allocatorSize);
    
    {
        STLAllocatorAdapter<int, FrameAllocator> adapter(&allocator);
        std::vector<int, STLAllocatorAdapter<int, FrameAllocator>> vec(adapter);
        
        for (int i = 0; i < 10; ++i)
        {
            vec.push_back(i);
        }
        
        CHECK(vec.size() == 10);
        CHECK(allocator.GetUsedSize() > 0);
    }
    
    // After vector goes out of scope, we can reset the frame
    // Note: In real usage, you'd reset at frame boundaries
    size_t usedBeforeReset = allocator.GetUsedSize();
    CHECK(usedBeforeReset > 0);
    
    allocator.ResetFrame();
    CHECK(allocator.GetUsedSize() == 0);
}

