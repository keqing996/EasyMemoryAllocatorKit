#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "EAllocKit/Util/Util.hpp"

using namespace EAllocKit;

TEST_SUITE("Util")
{
    TEST_CASE("TestAlignment - Basic")
    {
        CHECK(Util::UpAlignment(3, 4) == 4);
        CHECK(Util::UpAlignment(3, 8) == 8);
        CHECK(Util::UpAlignment(3, 16) == 16);
        CHECK(Util::UpAlignment(5, 4) == 8);
        CHECK(Util::UpAlignment(9, 8) == 16);
        CHECK(Util::UpAlignment(17, 16) == 32);
        CHECK(Util::UpAlignment(4, 4) == 4);
        CHECK(Util::UpAlignment(8, 8) == 8);
        CHECK(Util::UpAlignment(16, 16) == 16);

        CHECK(Util::UpAlignment<3, 4>() == 4);
        CHECK(Util::UpAlignment<3, 8>() == 8);
        CHECK(Util::UpAlignment<3, 16>() == 16);
        CHECK(Util::UpAlignment<5, 4>() == 8);
        CHECK(Util::UpAlignment<9, 8>() == 16);
        CHECK(Util::UpAlignment<17, 16>() == 32);
        CHECK(Util::UpAlignment<4, 4>() == 4);
        CHECK(Util::UpAlignment<8, 8>() == 8);
        CHECK(Util::UpAlignment<16, 16>() == 16);
    }

    TEST_CASE("TestAlignment - Edge Cases")
    {
        SUBCASE("Zero and one")
        {
            CHECK(Util::UpAlignment(0, 4) == 0);
            CHECK(Util::UpAlignment(1, 4) == 4);
            CHECK(Util::UpAlignment(1, 8) == 8);
            CHECK(Util::UpAlignment(1, 16) == 16);
        }
        
        SUBCASE("Large values")
        {
            CHECK(Util::UpAlignment(1000, 64) == 1024);
            CHECK(Util::UpAlignment(1024, 64) == 1024);
            CHECK(Util::UpAlignment(1025, 64) == 1088);
            CHECK(Util::UpAlignment(10000, 256) == 10240);
        }
        
        SUBCASE("Already aligned")
        {
            CHECK(Util::UpAlignment(32, 4) == 32);
            CHECK(Util::UpAlignment(64, 8) == 64);
            CHECK(Util::UpAlignment(128, 16) == 128);
            CHECK(Util::UpAlignment(256, 32) == 256);
        }
        
        SUBCASE("One byte before alignment")
        {
            CHECK(Util::UpAlignment(3, 4) == 4);
            CHECK(Util::UpAlignment(7, 8) == 8);
            CHECK(Util::UpAlignment(15, 16) == 16);
            CHECK(Util::UpAlignment(31, 32) == 32);
        }
        
        SUBCASE("Large alignments")
        {
            CHECK(Util::UpAlignment(100, 128) == 128);
            CHECK(Util::UpAlignment(200, 256) == 256);
            CHECK(Util::UpAlignment(1000, 512) == 1024);
        }
    }

    TEST_CASE("TestPowOfTwo - Basic")
    {
        CHECK(Util::UpAlignmentPowerOfTwo(2) == 4);
        CHECK(Util::UpAlignmentPowerOfTwo(5) == 8);
        CHECK(Util::UpAlignmentPowerOfTwo(9) == 16);
        CHECK(Util::UpAlignmentPowerOfTwo(16) == 16);
        CHECK(Util::UpAlignmentPowerOfTwo(55) == 64);
        CHECK(Util::UpAlignmentPowerOfTwo(129) == 256);
    }
    
    TEST_CASE("TestPowOfTwo - Edge Cases")
    {
        SUBCASE("Small values")
        {
            CHECK(Util::UpAlignmentPowerOfTwo(1) == 4);
            CHECK(Util::UpAlignmentPowerOfTwo(2) == 4);
            CHECK(Util::UpAlignmentPowerOfTwo(3) == 4);
            CHECK(Util::UpAlignmentPowerOfTwo(4) == 4);
        }
        
        SUBCASE("Powers of two")
        {
            CHECK(Util::UpAlignmentPowerOfTwo(4) == 4);
            CHECK(Util::UpAlignmentPowerOfTwo(8) == 8);
            CHECK(Util::UpAlignmentPowerOfTwo(16) == 16);
            CHECK(Util::UpAlignmentPowerOfTwo(32) == 32);
            CHECK(Util::UpAlignmentPowerOfTwo(64) == 64);
            CHECK(Util::UpAlignmentPowerOfTwo(128) == 128);
            CHECK(Util::UpAlignmentPowerOfTwo(256) == 256);
            CHECK(Util::UpAlignmentPowerOfTwo(512) == 512);
            CHECK(Util::UpAlignmentPowerOfTwo(1024) == 1024);
        }
        
        SUBCASE("One above power of two")
        {
            CHECK(Util::UpAlignmentPowerOfTwo(5) == 8);
            CHECK(Util::UpAlignmentPowerOfTwo(17) == 32);
            CHECK(Util::UpAlignmentPowerOfTwo(33) == 64);
            CHECK(Util::UpAlignmentPowerOfTwo(65) == 128);
            CHECK(Util::UpAlignmentPowerOfTwo(257) == 512);
        }
        
        SUBCASE("Middle values")
        {
            CHECK(Util::UpAlignmentPowerOfTwo(12) == 16);
            CHECK(Util::UpAlignmentPowerOfTwo(48) == 64);
            CHECK(Util::UpAlignmentPowerOfTwo(96) == 128);
            CHECK(Util::UpAlignmentPowerOfTwo(192) == 256);
        }
        
        SUBCASE("Large values")
        {
            CHECK(Util::UpAlignmentPowerOfTwo(1000) == 1024);
            CHECK(Util::UpAlignmentPowerOfTwo(2000) == 2048);
            CHECK(Util::UpAlignmentPowerOfTwo(5000) == 8192);
            CHECK(Util::UpAlignmentPowerOfTwo(10000) == 16384);
        }
    }
    
    TEST_CASE("TestAlignment - Consistency")
    {
        SUBCASE("Runtime vs compile-time alignment")
        {
            // Verify that runtime and compile-time versions give same results
            CHECK(Util::UpAlignment(5, 8) == Util::UpAlignment<5, 8>());
            CHECK(Util::UpAlignment(13, 16) == Util::UpAlignment<13, 16>());
            CHECK(Util::UpAlignment(27, 32) == Util::UpAlignment<27, 32>());
        }
        
        SUBCASE("Multiple alignments in sequence")
        {
            size_t value = 1;
            value = Util::UpAlignment(value, 4);
            CHECK(value == 4);
            
            value = Util::UpAlignment(value + 1, 8);
            CHECK(value == 8);
            
            value = Util::UpAlignment(value + 1, 16);
            CHECK(value == 16);
        }
    }
    
    TEST_CASE("TestPtrOffset")
    {
        SUBCASE("Basic pointer arithmetic")
        {
            uint8_t buffer[256];
            uint8_t* base = buffer;
            
            auto* offset1 = Util::PtrOffsetBytes(base, ptrdiff_t(10));
            CHECK(offset1 == base + 10);
            CHECK(Util::ToAddr(offset1) == Util::ToAddr(base) + 10);
            
            auto* offset2 = Util::PtrOffsetBytes(base, ptrdiff_t(100));
            CHECK(offset2 == base + 100);
            CHECK(Util::ToAddr(offset2) == Util::ToAddr(base) + 100);
        }
        
        SUBCASE("Zero offset")
        {
            uint8_t buffer[128];
            uint8_t* base = buffer;
            
            auto* offset = Util::PtrOffsetBytes(base, ptrdiff_t(0));
            CHECK(offset == base);
        }
    }
    
    TEST_CASE("TestGetPaddedSize")
    {
        SUBCASE("Various type sizes with different alignments")
        {
            // uint32_t with 4-byte alignment
            size_t size1 = Util::GetPaddedSize<uint32_t>(4);
            CHECK(size1 == 4);
            
            // uint32_t with 8-byte alignment
            size_t size2 = Util::GetPaddedSize<uint32_t>(8);
            CHECK(size2 == 8);
            
            // uint64_t with 8-byte alignment
            size_t size3 = Util::GetPaddedSize<uint64_t>(8);
            CHECK(size3 == 8);
            
            // uint64_t with 16-byte alignment
            size_t size4 = Util::GetPaddedSize<uint64_t>(16);
            CHECK(size4 == 16);
        }
        
        SUBCASE("Compile-time version")
        {
            constexpr size_t size1 = Util::GetPaddedSize<uint32_t, 4>();
            CHECK(size1 == 4);
            
            constexpr size_t size2 = Util::GetPaddedSize<uint64_t, 8>();
            CHECK(size2 == 8);
            
            constexpr size_t size3 = Util::GetPaddedSize<uint32_t, 16>();
            CHECK(size3 == 16);
        }
    }
}
