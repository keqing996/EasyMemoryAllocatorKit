
#include "DocTest.h"
#include "Allocator/Util/Util.hpp"

using namespace MemoryPool;

TEST_CASE("TestAlignment")
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

TEST_CASE("TestPowOfTwo")
{
    CHECK(Util::UpAlignmentPowerOfTwo(2) == 4);
    CHECK(Util::UpAlignmentPowerOfTwo(5) == 8);
    CHECK(Util::UpAlignmentPowerOfTwo(9) == 16);
    CHECK(Util::UpAlignmentPowerOfTwo(16) == 16);
    CHECK(Util::UpAlignmentPowerOfTwo(55) == 64);
    CHECK(Util::UpAlignmentPowerOfTwo(129) == 256);
}