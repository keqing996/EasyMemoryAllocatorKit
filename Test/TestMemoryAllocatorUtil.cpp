#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "EAllocKit/Util/Util.hpp"

using namespace EAllocKit;

TEST_SUITE("MemoryAllocatorUtil")
{
    TEST_CASE("TestAlignment - Basic")
    {
        CHECK(MemoryAllocatorUtil::UpAlignment(3, 4) == 4);
        CHECK(MemoryAllocatorUtil::UpAlignment(3, 8) == 8);
        CHECK(MemoryAllocatorUtil::UpAlignment(3, 16) == 16);
        CHECK(MemoryAllocatorUtil::UpAlignment(5, 4) == 8);
        CHECK(MemoryAllocatorUtil::UpAlignment(9, 8) == 16);
        CHECK(MemoryAllocatorUtil::UpAlignment(17, 16) == 32);
        CHECK(MemoryAllocatorUtil::UpAlignment(4, 4) == 4);
        CHECK(MemoryAllocatorUtil::UpAlignment(8, 8) == 8);
        CHECK(MemoryAllocatorUtil::UpAlignment(16, 16) == 16);

        CHECK(MemoryAllocatorUtil::UpAlignment<3, 4>() == 4);
        CHECK(MemoryAllocatorUtil::UpAlignment<3, 8>() == 8);
        CHECK(MemoryAllocatorUtil::UpAlignment<3, 16>() == 16);
        CHECK(MemoryAllocatorUtil::UpAlignment<5, 4>() == 8);
        CHECK(MemoryAllocatorUtil::UpAlignment<9, 8>() == 16);
        CHECK(MemoryAllocatorUtil::UpAlignment<17, 16>() == 32);
        CHECK(MemoryAllocatorUtil::UpAlignment<4, 4>() == 4);
        CHECK(MemoryAllocatorUtil::UpAlignment<8, 8>() == 8);
        CHECK(MemoryAllocatorUtil::UpAlignment<16, 16>() == 16);
    }

    TEST_CASE("TestAlignment - Edge Cases")
    {
        SUBCASE("Zero and one")
        {
            CHECK(MemoryAllocatorUtil::UpAlignment(0, 4) == 0);
            CHECK(MemoryAllocatorUtil::UpAlignment(1, 4) == 4);
            CHECK(MemoryAllocatorUtil::UpAlignment(1, 8) == 8);
            CHECK(MemoryAllocatorUtil::UpAlignment(1, 16) == 16);
        }
        
        SUBCASE("Large values")
        {
            CHECK(MemoryAllocatorUtil::UpAlignment(1000, 64) == 1024);
            CHECK(MemoryAllocatorUtil::UpAlignment(1024, 64) == 1024);
            CHECK(MemoryAllocatorUtil::UpAlignment(1025, 64) == 1088);
            CHECK(MemoryAllocatorUtil::UpAlignment(10000, 256) == 10240);
        }
        
        SUBCASE("Already aligned")
        {
            CHECK(MemoryAllocatorUtil::UpAlignment(32, 4) == 32);
            CHECK(MemoryAllocatorUtil::UpAlignment(64, 8) == 64);
            CHECK(MemoryAllocatorUtil::UpAlignment(128, 16) == 128);
            CHECK(MemoryAllocatorUtil::UpAlignment(256, 32) == 256);
        }
        
        SUBCASE("One byte before alignment")
        {
            CHECK(MemoryAllocatorUtil::UpAlignment(3, 4) == 4);
            CHECK(MemoryAllocatorUtil::UpAlignment(7, 8) == 8);
            CHECK(MemoryAllocatorUtil::UpAlignment(15, 16) == 16);
            CHECK(MemoryAllocatorUtil::UpAlignment(31, 32) == 32);
        }
        
        SUBCASE("Large alignments")
        {
            CHECK(MemoryAllocatorUtil::UpAlignment(100, 128) == 128);
            CHECK(MemoryAllocatorUtil::UpAlignment(200, 256) == 256);
            CHECK(MemoryAllocatorUtil::UpAlignment(1000, 512) == 1024);
        }
    }

    TEST_CASE("TestPowOfTwo - Basic")
    {
        CHECK(MemoryAllocatorUtil::UpAlignmentPowerOfTwo(2) == 4);
        CHECK(MemoryAllocatorUtil::UpAlignmentPowerOfTwo(5) == 8);
        CHECK(MemoryAllocatorUtil::UpAlignmentPowerOfTwo(9) == 16);
        CHECK(MemoryAllocatorUtil::UpAlignmentPowerOfTwo(16) == 16);
        CHECK(MemoryAllocatorUtil::UpAlignmentPowerOfTwo(55) == 64);
        CHECK(MemoryAllocatorUtil::UpAlignmentPowerOfTwo(129) == 256);
    }
    
    TEST_CASE("TestPowOfTwo - Edge Cases")
    {
        SUBCASE("Small values")
        {
            CHECK(MemoryAllocatorUtil::UpAlignmentPowerOfTwo(1) == 4);
            CHECK(MemoryAllocatorUtil::UpAlignmentPowerOfTwo(2) == 4);
            CHECK(MemoryAllocatorUtil::UpAlignmentPowerOfTwo(3) == 4);
            CHECK(MemoryAllocatorUtil::UpAlignmentPowerOfTwo(4) == 4);
        }
        
        SUBCASE("Powers of two")
        {
            CHECK(MemoryAllocatorUtil::UpAlignmentPowerOfTwo(4) == 4);
            CHECK(MemoryAllocatorUtil::UpAlignmentPowerOfTwo(8) == 8);
            CHECK(MemoryAllocatorUtil::UpAlignmentPowerOfTwo(16) == 16);
            CHECK(MemoryAllocatorUtil::UpAlignmentPowerOfTwo(32) == 32);
            CHECK(MemoryAllocatorUtil::UpAlignmentPowerOfTwo(64) == 64);
            CHECK(MemoryAllocatorUtil::UpAlignmentPowerOfTwo(128) == 128);
            CHECK(MemoryAllocatorUtil::UpAlignmentPowerOfTwo(256) == 256);
            CHECK(MemoryAllocatorUtil::UpAlignmentPowerOfTwo(512) == 512);
            CHECK(MemoryAllocatorUtil::UpAlignmentPowerOfTwo(1024) == 1024);
        }
        
        SUBCASE("One above power of two")
        {
            CHECK(MemoryAllocatorUtil::UpAlignmentPowerOfTwo(5) == 8);
            CHECK(MemoryAllocatorUtil::UpAlignmentPowerOfTwo(17) == 32);
            CHECK(MemoryAllocatorUtil::UpAlignmentPowerOfTwo(33) == 64);
            CHECK(MemoryAllocatorUtil::UpAlignmentPowerOfTwo(65) == 128);
            CHECK(MemoryAllocatorUtil::UpAlignmentPowerOfTwo(257) == 512);
        }
        
        SUBCASE("Middle values")
        {
            CHECK(MemoryAllocatorUtil::UpAlignmentPowerOfTwo(12) == 16);
            CHECK(MemoryAllocatorUtil::UpAlignmentPowerOfTwo(48) == 64);
            CHECK(MemoryAllocatorUtil::UpAlignmentPowerOfTwo(96) == 128);
            CHECK(MemoryAllocatorUtil::UpAlignmentPowerOfTwo(192) == 256);
        }
        
        SUBCASE("Large values")
        {
            CHECK(MemoryAllocatorUtil::UpAlignmentPowerOfTwo(1000) == 1024);
            CHECK(MemoryAllocatorUtil::UpAlignmentPowerOfTwo(2000) == 2048);
            CHECK(MemoryAllocatorUtil::UpAlignmentPowerOfTwo(5000) == 8192);
            CHECK(MemoryAllocatorUtil::UpAlignmentPowerOfTwo(10000) == 16384);
        }
    }
    
    TEST_CASE("TestAlignment - Consistency")
    {
        SUBCASE("Runtime vs compile-time alignment")
        {
            // Verify that runtime and compile-time versions give same results
            CHECK(MemoryAllocatorUtil::UpAlignment(5, 8) == MemoryAllocatorUtil::UpAlignment<5, 8>());
            CHECK(MemoryAllocatorUtil::UpAlignment(13, 16) == MemoryAllocatorUtil::UpAlignment<13, 16>());
            CHECK(MemoryAllocatorUtil::UpAlignment(27, 32) == MemoryAllocatorUtil::UpAlignment<27, 32>());
        }
        
        SUBCASE("Multiple alignments in sequence")
        {
            size_t value = 1;
            value = MemoryAllocatorUtil::UpAlignment(value, 4);
            CHECK(value == 4);
            
            value = MemoryAllocatorUtil::UpAlignment(value + 1, 8);
            CHECK(value == 8);
            
            value = MemoryAllocatorUtil::UpAlignment(value + 1, 16);
            CHECK(value == 16);
        }
    }
    
    TEST_CASE("TestPtrOffset")
    {
        SUBCASE("Basic pointer arithmetic")
        {
            uint8_t buffer[256];
            uint8_t* base = buffer;
            
            auto* offset1 = MemoryAllocatorUtil::PtrOffsetBytes(base, ptrdiff_t(10));
            CHECK(offset1 == base + 10);
            CHECK(MemoryAllocatorUtil::ToAddr(offset1) == MemoryAllocatorUtil::ToAddr(base) + 10);
            
            auto* offset2 = MemoryAllocatorUtil::PtrOffsetBytes(base, ptrdiff_t(100));
            CHECK(offset2 == base + 100);
            CHECK(MemoryAllocatorUtil::ToAddr(offset2) == MemoryAllocatorUtil::ToAddr(base) + 100);
        }
        
        SUBCASE("Zero offset")
        {
            uint8_t buffer[128];
            uint8_t* base = buffer;
            
            auto* offset = MemoryAllocatorUtil::PtrOffsetBytes(base, ptrdiff_t(0));
            CHECK(offset == base);
        }
    }
    
    TEST_CASE("TestGetPaddedSize")
    {
        SUBCASE("Various type sizes with different alignments")
        {
            // uint32_t with 4-byte alignment
            size_t size1 = MemoryAllocatorUtil::GetPaddedSize<uint32_t>(4);
            CHECK(size1 == 4);
            
            // uint32_t with 8-byte alignment
            size_t size2 = MemoryAllocatorUtil::GetPaddedSize<uint32_t>(8);
            CHECK(size2 == 8);
            
            // uint64_t with 8-byte alignment
            size_t size3 = MemoryAllocatorUtil::GetPaddedSize<uint64_t>(8);
            CHECK(size3 == 8);
            
            // uint64_t with 16-byte alignment
            size_t size4 = MemoryAllocatorUtil::GetPaddedSize<uint64_t>(16);
            CHECK(size4 == 16);
        }
        
        SUBCASE("Compile-time version")
        {
            constexpr size_t size1 = MemoryAllocatorUtil::GetPaddedSize<uint32_t, 4>();
            CHECK(size1 == 4);
            
            constexpr size_t size2 = MemoryAllocatorUtil::GetPaddedSize<uint64_t, 8>();
            CHECK(size2 == 8);
            
            constexpr size_t size3 = MemoryAllocatorUtil::GetPaddedSize<uint32_t, 16>();
            CHECK(size3 == 16);
        }
    }
}
