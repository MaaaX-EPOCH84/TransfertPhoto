#include "core/byte_compare.hpp"

#include "memory_source.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <stop_token>
#include <vector>

// Noms de tests sans accents : voir version_test.cpp.

using test_support::MemorySource;
using transfertphoto::compare_block_size;
using transfertphoto::compare_files;
using transfertphoto::Comparison;
using transfertphoto::CompareResult;

namespace {

using Bytes = std::vector<std::uint8_t>;

Bytes content(std::size_t size)
{
    Bytes bytes(size);
    for (std::size_t i = 0; i < size; ++i)
        bytes[i] = static_cast<std::uint8_t>((i * 7) % 251);
    return bytes;
}

constexpr std::size_t size = 3 * 1024 * 1024 + 17;

// Deux sources : la carte et la destination.
// Les deux copies sont d'abord identiques ; chaque test les abîme à sa façon.
struct Fixture {
    MemorySource card;
    MemorySource library;

    Fixture()
    {
        card.contents["DCIM/a.mp4"] = content(size);
        library.contents["2023/12/25/a.mp4"] = content(size);
    }

    CompareResult compare(std::uint64_t card_size = size, std::uint64_t library_size = size,
                          std::stop_token stop = {}, const std::function<void(std::uint64_t)>& on_bytes = {})
    {
        return compare_files({card, "DCIM/a.mp4", card_size}, {library, "2023/12/25/a.mp4", library_size}, stop,
                             on_bytes);
    }
};

} // namespace

TEST_CASE("deux fichiers identiques le sont, lus par blocs")
{
    Fixture fixture;
    std::uint64_t bytes = 0;
    const auto result = fixture.compare(size, size, {}, [&](std::uint64_t n) { bytes += n; });
    CHECK(result.outcome == Comparison::identical);
    CHECK(bytes == 2 * size);
    CHECK(fixture.card.largest_request == compare_block_size);
}

TEST_CASE("un seul octet different se trouve, ou qu'il soit")
{
    for (const std::size_t where : {std::size_t{0}, compare_block_size - 1, compare_block_size, size / 2, size - 1}) {
        INFO("octet " << where);
        Fixture fixture;
        fixture.library.contents["2023/12/25/a.mp4"][where] ^= 0x01;
        const auto result = fixture.compare();
        CHECK(result.outcome == Comparison::different);
        CHECK(result.first_difference == where);
    }
}

TEST_CASE("des tailles differentes concluent sans rien lire")
{
    Fixture fixture;
    const auto result = fixture.compare(size, size - 1);
    CHECK(result.outcome == Comparison::different);
    CHECK(fixture.card.bytes_read == 0);
    CHECK(fixture.library.bytes_read == 0);
}

TEST_CASE("un fichier plus court ou plus long que sa taille relevee a change")
{
    SECTION("plus court")
    {
        Fixture fixture;
        fixture.card.contents["DCIM/a.mp4"].resize(size - 5);
        CHECK(fixture.compare().outcome == Comparison::changed);
    }
    SECTION("plus long")
    {
        Fixture fixture;
        fixture.library.contents["2023/12/25/a.mp4"].push_back(0);
        CHECK(fixture.compare().outcome == Comparison::changed);
    }
}

TEST_CASE("une lecture en echec se signale avec son cote")
{
    SECTION("le premier")
    {
        Fixture fixture;
        fixture.card.failing.insert("DCIM/a.mp4");
        const auto result = fixture.compare();
        CHECK(result.outcome == Comparison::unreadable);
        CHECK(result.first_failed);
        CHECK(result.error == std::errc::io_error);
    }
    SECTION("le second")
    {
        Fixture fixture;
        fixture.library.failing.insert("2023/12/25/a.mp4");
        const auto result = fixture.compare();
        CHECK(result.outcome == Comparison::unreadable);
        CHECK_FALSE(result.first_failed);
    }
}

TEST_CASE("la comparaison s'interrompt entre deux blocs")
{
    Fixture fixture;
    std::stop_source stop;
    const auto result = fixture.compare(size, size, stop.get_token(), [&](std::uint64_t) { stop.request_stop(); });
    CHECK(result.outcome == Comparison::interrupted);
    CHECK(fixture.card.bytes_read == compare_block_size);
}

TEST_CASE("deux fichiers vides sont identiques")
{
    Fixture fixture;
    fixture.card.contents["DCIM/a.mp4"] = {};
    fixture.library.contents["2023/12/25/a.mp4"] = {};
    CHECK(fixture.compare(0, 0).outcome == Comparison::identical);
}
