#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "../Src/Util.hpp"

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
}