#include <catch2/catch_test_macros.hpp>

#include "itch.hpp"

TEST_CASE("library links into test binary") {
    REQUIRE(itch::version() == 1);
}
