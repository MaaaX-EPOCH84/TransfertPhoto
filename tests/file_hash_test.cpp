#include "core/file_hash.hpp"

#include "memory_source.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <stop_token>
#include <string>
#include <vector>

// Noms de tests sans accents : voir version_test.cpp.
//
// Empreintes de référence calculées par xxhsum -H2 de xxHash v0.8.4, sur les
// mêmes contenus.

using test_support::MemorySource;
using transfertphoto::hash_bytes;
using transfertphoto::hash_block_size;
using transfertphoto::hash_file;
using transfertphoto::to_hex;

namespace {

// Octet i = (i × 7) mod 251, comme le motif des autres tests.
std::vector<std::uint8_t> pattern(std::size_t size)
{
    std::vector<std::uint8_t> content(size);
    for (std::size_t i = 0; i < size; ++i)
        content[i] = static_cast<std::uint8_t>((i * 7) % 251);
    return content;
}

std::span<const std::byte> as_bytes(const std::vector<std::uint8_t>& content)
{
    return std::as_bytes(std::span(content));
}

constexpr std::size_t pattern_size = 3 * 1024 * 1024 + 17;

} // namespace

TEST_CASE("l'empreinte est celle de xxhsum")
{
    CHECK(to_hex(hash_bytes({})) == "99aa06d3014798d86001c324468d497f");
    const std::string abc = "abc";
    CHECK(to_hex(hash_bytes(std::as_bytes(std::span(abc)))) == "06b05ab6733a618578af5f94892f3950");
    CHECK(to_hex(hash_bytes(as_bytes(pattern(pattern_size)))) == "c88010b09a16a1537a7a986a9563baf0");
}

TEST_CASE("un fichier lu par blocs a l'empreinte de son contenu lu d'un coup")
{
    MemorySource source;
    source.contents["film.mp4"] = pattern(pattern_size);

    std::vector<std::uint64_t> blocks;
    const auto result = hash_file(source, "film.mp4", pattern_size, {}, transfertphoto::whole_file,
                                  [&](std::uint64_t bytes) { blocks.push_back(bytes); });
    REQUIRE(result.hash);
    CHECK(to_hex(*result.hash) == "c88010b09a16a1537a7a986a9563baf0");
    CHECK_FALSE(result.source_error);
    CHECK_FALSE(result.interrupted);
    CHECK_FALSE(result.changed);
    // Quatre blocs : trois de 1 Mio et le reste ; jamais plus d'un bloc lu à la fois.
    CHECK(blocks == std::vector<std::uint64_t>{hash_block_size, hash_block_size, hash_block_size, 17});
    CHECK(source.largest_request == hash_block_size);
}

TEST_CASE("l'empreinte partielle ne lit que le debut")
{
    MemorySource source;
    source.contents["film.mp4"] = pattern(pattern_size);
    const auto result = hash_file(source, "film.mp4", pattern_size, {}, 64 * 1024);
    REQUIRE(result.hash);
    CHECK(to_hex(*result.hash) == "da25096a04743fc0db485bf0653067e1");
    CHECK(source.largest_request == 64 * 1024);

    // Plus court que la limite : tout le fichier.
    source.contents["court.jpg"] = {'a', 'b', 'c'};
    const auto small = hash_file(source, "court.jpg", 3, {}, 64 * 1024);
    REQUIRE(small.hash);
    CHECK(to_hex(*small.hash) == "06b05ab6733a618578af5f94892f3950");
}

TEST_CASE("un fichier vide a l'empreinte du contenu vide, sans lecture")
{
    MemorySource source;
    source.contents["vide.jpg"] = {};
    const auto result = hash_file(source, "vide.jpg", 0, {});
    REQUIRE(result.hash);
    CHECK(to_hex(*result.hash) == "99aa06d3014798d86001c324468d497f");
    CHECK(source.reads["vide.jpg"] == 0);
}

TEST_CASE("une source qui lache ne donne pas d'empreinte")
{
    MemorySource source;
    source.contents["film.mp4"] = pattern(1000);
    source.failing.insert("film.mp4");
    const auto result = hash_file(source, "film.mp4", 1000, {});
    CHECK_FALSE(result.hash);
    CHECK(result.source_error == std::errc::io_error);
}

TEST_CASE("un fichier plus court que sa taille annoncee a change")
{
    MemorySource source;
    source.contents["film.mp4"] = pattern(1000);
    const auto result = hash_file(source, "film.mp4", 5000, {});
    CHECK_FALSE(result.hash);
    CHECK(result.changed);
    CHECK_FALSE(result.source_error);
}

TEST_CASE("l'empreinte s'interrompt entre deux blocs")
{
    MemorySource source;
    source.contents["film.mp4"] = pattern(pattern_size);
    std::stop_source stop;
    int blocks = 0;
    const auto result = hash_file(source, "film.mp4", pattern_size, stop.get_token(), transfertphoto::whole_file,
                                  [&](std::uint64_t) {
                                      if (++blocks == 1)
                                          stop.request_stop();
                                  });
    CHECK_FALSE(result.hash);
    CHECK(result.interrupted);
    CHECK(blocks == 1);
    CHECK(source.reads["film.mp4"] == 1);
}
