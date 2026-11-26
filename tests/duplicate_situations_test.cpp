#include "core/duplicate_situations.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <vector>

// Noms de tests sans accents : voir version_test.cpp.

using transfertphoto::DuplicateGroup;
using transfertphoto::DuplicateSituation;
using transfertphoto::situations_of;
using transfertphoto::Side;

namespace {

DuplicateGroup group(std::size_t in_source, std::size_t in_destination)
{
    DuplicateGroup result;
    for (std::size_t i = 0; i < in_source; ++i)
        result.members.push_back({Side::source, i});
    for (std::size_t i = 0; i < in_destination; ++i)
        result.members.push_back({Side::destination, i});
    return result;
}

std::vector<DuplicateSituation> questions(std::size_t in_source, std::size_t in_destination)
{
    return situations_of(group(in_source, in_destination)).questions();
}

using enum DuplicateSituation;

} // namespace

TEST_CASE("chaque situation seule")
{
    CHECK(questions(2, 0) == std::vector{within_source});
    CHECK(questions(3, 0) == std::vector{within_source});
    CHECK(questions(0, 2) == std::vector{within_destination});
    CHECK(questions(1, 1) == std::vector{between});
}

TEST_CASE("groupes meles : une question par situation presente")
{
    // Une copie de chaque côté rend sans objet la question dans la source.
    CHECK(questions(2, 1) == std::vector{between});
    CHECK(questions(1, 2) == std::vector{between, within_destination});
    CHECK(questions(3, 3) == std::vector{between, within_destination});
}

TEST_CASE("les copies se comptent de chaque cote")
{
    const auto situations = situations_of(group(2, 3));
    CHECK(situations.source_copies == 2);
    CHECK(situations.destination_copies == 3);
    CHECK(situations.has(between));
    CHECK(situations.has(within_destination));
    CHECK_FALSE(situations.has(within_source));
}
