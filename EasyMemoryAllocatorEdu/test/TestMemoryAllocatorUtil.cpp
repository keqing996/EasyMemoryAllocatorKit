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
    
    TEST_CASE("TestIsPowerOfTwo - Comprehensive")
    {
        SUBCASE("Valid powers of two")
        {
            CHECK(Util::IsPowerOfTwo(1) == true);
            CHECK(Util::IsPowerOfTwo(2) == true);
            CHECK(Util::IsPowerOfTwo(4) == true);
            CHECK(Util::IsPowerOfTwo(8) == true);
            CHECK(Util::IsPowerOfTwo(16) == true);
            CHECK(Util::IsPowerOfTwo(32) == true);
            CHECK(Util::IsPowerOfTwo(64) == true);
            CHECK(Util::IsPowerOfTwo(128) == true);
            CHECK(Util::IsPowerOfTwo(256) == true);
            CHECK(Util::IsPowerOfTwo(512) == true);
            CHECK(Util::IsPowerOfTwo(1024) == true);
        }
        
        SUBCASE("Invalid values")
        {
            CHECK(Util::IsPowerOfTwo(0) == false);
            CHECK(Util::IsPowerOfTwo(3) == false);
            CHECK(Util::IsPowerOfTwo(5) == false);
            CHECK(Util::IsPowerOfTwo(6) == false);
            CHECK(Util::IsPowerOfTwo(7) == false);
            CHECK(Util::IsPowerOfTwo(9) == false);
            CHECK(Util::IsPowerOfTwo(12) == false);
            CHECK(Util::IsPowerOfTwo(15) == false);
            CHECK(Util::IsPowerOfTwo(17) == false);
            CHECK(Util::IsPowerOfTwo(100) == false);
            CHECK(Util::IsPowerOfTwo(1000) == false);
        }
        
        SUBCASE("Large powers of two")
        {
            CHECK(Util::IsPowerOfTwo(2048) == true);
            CHECK(Util::IsPowerOfTwo(4096) == true);
            CHECK(Util::IsPowerOfTwo(8192) == true);
            CHECK(Util::IsPowerOfTwo(16384) == true);
            CHECK(Util::IsPowerOfTwo(32768) == true);
            CHECK(Util::IsPowerOfTwo(65536) == true);
            CHECK(Util::IsPowerOfTwo(1048576) == true); // 1MB
            CHECK(Util::IsPowerOfTwo(1073741824) == true); // 1GB
        }
    }
    
    TEST_CASE("TestToAddr - Address Conversion")
    {
        SUBCASE("Valid pointer conversion")
        {
            int value = 42;
            int* ptr = &value;
            size_t addr = Util::ToAddr(ptr);
            CHECK(addr == reinterpret_cast<size_t>(ptr));
            CHECK(addr != 0);
        }
        
        SUBCASE("Different pointer types")
        {
            double d = 3.14;
            char c = 'A';
            int arr[10];
            
            CHECK(Util::ToAddr(&d) == reinterpret_cast<size_t>(&d));
            CHECK(Util::ToAddr(&c) == reinterpret_cast<size_t>(&c));
            CHECK(Util::ToAddr(arr) == reinterpret_cast<size_t>(arr));
        }
    }
    
    TEST_CASE("TestPtrOffsetBytes - Pointer Arithmetic")
    {
        SUBCASE("Basic offset operations")
        {
            char buffer[1000];
            char* base = buffer;
            
            char* offset0 = Util::PtrOffsetBytes(base, 0);
            char* offset10 = Util::PtrOffsetBytes(base, 10);
            char* offset100 = Util::PtrOffsetBytes(base, 100);
            
            CHECK(offset0 == base);
            CHECK(offset10 == base + 10);
            CHECK(offset100 == base + 100);
        }
        
        SUBCASE("Large offset operations")
        {
            char* base = new char[10000];
            
            char* offset1k = Util::PtrOffsetBytes(base, 1024);
            char* offset5k = Util::PtrOffsetBytes(base, 5120);
            
            CHECK(offset1k == base + 1024);
            CHECK(offset5k == base + 5120);
            
            delete[] base;
        }
        
        SUBCASE("Negative offset")
        {
            char buffer[1000];
            char* middle = buffer + 500;
            
            char* before = Util::PtrOffsetBytes(middle, -100);
            CHECK(before == middle - 100);
        }
    }
    
    TEST_CASE("TestAlignment - Stress Testing")
    {
        SUBCASE("Random alignment scenarios")
        {
            std::vector<size_t> alignments = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512};
            std::vector<size_t> sizes = {1, 3, 7, 15, 31, 63, 127, 255, 511, 1023};
            
            for (size_t alignment : alignments) {
                for (size_t size : sizes) {
                    size_t aligned = Util::UpAlignment(size, alignment);
                    
                    // Verify alignment
                    CHECK(aligned % alignment == 0);
                    
                    // Verify it's the smallest aligned value >= size
                    CHECK(aligned >= size);
                    if (aligned > alignment) {
                        CHECK((aligned - alignment) < size);
                    }
                }
            }
        }
        
        SUBCASE("Boundary conditions")
        {
            CHECK(Util::UpAlignment(SIZE_MAX - 1, 2) == SIZE_MAX - 1); // Even number
            CHECK(Util::UpAlignment(UINT32_MAX, 1) == UINT32_MAX);
            
            // Test very large alignments
            CHECK(Util::UpAlignment(1, 1024) == 1024);
            CHECK(Util::UpAlignment(1000, 1024) == 1024);
            CHECK(Util::UpAlignment(1024, 1024) == 1024);
            CHECK(Util::UpAlignment(1025, 1024) == 2048);
        }
    }
    
    TEST_CASE("TestUtility - Integration Tests")
    {
        SUBCASE("Alignment and padding integration")
        {
            // Test that GetPaddedSize and UpAlignment work consistently
            struct TestStruct {
                char a;
                int b;
                double c;
            };
            
            size_t alignments[] = {1, 4, 8, 16, 32};
            
            for (size_t alignment : alignments) {
                size_t paddedSize = Util::GetPaddedSize<TestStruct>(alignment);
                size_t manualPadded = Util::UpAlignment(sizeof(TestStruct), alignment);
                
                CHECK(paddedSize == manualPadded);
                CHECK(paddedSize % alignment == 0);
                CHECK(paddedSize >= sizeof(TestStruct));
            }
        }
        
        SUBCASE("Pointer arithmetic consistency")
        {
            char buffer[1000];
            char* base = buffer;
            
            for (size_t i = 0; i < 10; i++) {
                std::ptrdiff_t offset = static_cast<std::ptrdiff_t>(i * 100);
                char* ptr = Util::PtrOffsetBytes(base, offset);
                size_t addr1 = Util::ToAddr(ptr);
                size_t addr2 = Util::ToAddr(base) + static_cast<size_t>(offset);
                
                CHECK(addr1 == addr2);
            }
        }
        
        SUBCASE("Power of two validation for common alignments")
        {
            size_t commonAlignments[] = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024};
            
            for (size_t alignment : commonAlignments) {
                CHECK(Util::IsPowerOfTwo(alignment) == true);
                
                // Test non-power-of-two variants
                // Skip alignment - 1 for small values where it might be a power of 2
                if (alignment > 2) {
                    CHECK(Util::IsPowerOfTwo(alignment - 1) == false);
                }
                if (alignment > 1) {
                    CHECK(Util::IsPowerOfTwo(alignment + 1) == false);
                }
            }
        }
    }
    
    TEST_CASE("TestRoundUpToPowerOf2")
    {
        SUBCASE("Basic rounding")
        {
            CHECK(Util::RoundUpToPowerOf2(0) == 1);
            CHECK(Util::RoundUpToPowerOf2(1) == 1);
            CHECK(Util::RoundUpToPowerOf2(2) == 2);
            CHECK(Util::RoundUpToPowerOf2(3) == 4);
            CHECK(Util::RoundUpToPowerOf2(4) == 4);
            CHECK(Util::RoundUpToPowerOf2(5) == 8);
            CHECK(Util::RoundUpToPowerOf2(8) == 8);
            CHECK(Util::RoundUpToPowerOf2(9) == 16);
            CHECK(Util::RoundUpToPowerOf2(15) == 16);
            CHECK(Util::RoundUpToPowerOf2(16) == 16);
            CHECK(Util::RoundUpToPowerOf2(17) == 32);
        }
        
        SUBCASE("Large values")
        {
            CHECK(Util::RoundUpToPowerOf2(1000) == 1024);
            CHECK(Util::RoundUpToPowerOf2(1024) == 1024);
            CHECK(Util::RoundUpToPowerOf2(1025) == 2048);
            CHECK(Util::RoundUpToPowerOf2(100000) == 131072);
        }
    }
    
    TEST_CASE("TestLog2")
    {
        SUBCASE("Powers of two")
        {
            CHECK(Util::Log2(1) == 0);
            CHECK(Util::Log2(2) == 1);
            CHECK(Util::Log2(4) == 2);
            CHECK(Util::Log2(8) == 3);
            CHECK(Util::Log2(16) == 4);
            CHECK(Util::Log2(32) == 5);
            CHECK(Util::Log2(64) == 6);
            CHECK(Util::Log2(128) == 7);
            CHECK(Util::Log2(256) == 8);
            CHECK(Util::Log2(512) == 9);
            CHECK(Util::Log2(1024) == 10);
        }
        
        SUBCASE("Non-powers of two")
        {
            CHECK(Util::Log2(3) == 1);   // floor(log2(3))
            CHECK(Util::Log2(5) == 2);   // floor(log2(5))
            CHECK(Util::Log2(7) == 2);   // floor(log2(7))
            CHECK(Util::Log2(15) == 3);  // floor(log2(15))
            CHECK(Util::Log2(31) == 4);  // floor(log2(31))
        }
    }
    
    TEST_CASE("TestUpAlignmentPowerOfTwo")
    {
        SUBCASE("Small values")
        {
            CHECK(Util::UpAlignmentPowerOfTwo(0) == 4);
            CHECK(Util::UpAlignmentPowerOfTwo(1) == 4);
            CHECK(Util::UpAlignmentPowerOfTwo(2) == 4);
            CHECK(Util::UpAlignmentPowerOfTwo(3) == 4);
            CHECK(Util::UpAlignmentPowerOfTwo(4) == 4);
            CHECK(Util::UpAlignmentPowerOfTwo(5) == 8);
        }
        
        SUBCASE("Larger values")
        {
            CHECK(Util::UpAlignmentPowerOfTwo(8) == 8);
            CHECK(Util::UpAlignmentPowerOfTwo(9) == 16);
            CHECK(Util::UpAlignmentPowerOfTwo(16) == 16);
            CHECK(Util::UpAlignmentPowerOfTwo(17) == 32);
            CHECK(Util::UpAlignmentPowerOfTwo(32) == 32);
            CHECK(Util::UpAlignmentPowerOfTwo(33) == 64);
        }
    }
}
