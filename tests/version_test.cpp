#include "core/version.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("la version du cœur est celle déclarée par la construction")
{
    CHECK(transfertphoto::version() == EXPECTED_VERSION);
}
