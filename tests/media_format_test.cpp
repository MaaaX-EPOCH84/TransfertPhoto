#include "core/media_format.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <string_view>

// Noms de tests sans accents : voir version_test.cpp.

using transfertphoto::format_from_extension;
using transfertphoto::kind_of;
using transfertphoto::media_extensions;
using transfertphoto::MediaFormat;
using transfertphoto::MediaKind;

namespace {

std::string to_upper(std::string_view text)
{
    std::string result(text);
    std::ranges::transform(result, result.begin(), [](char c) {
        return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
    });
    return result;
}

} // namespace

TEST_CASE("la table compte 17 extensions photo et 8 video, sans doublon")
{
    std::set<std::string_view> seen;
    std::size_t photos = 0;
    std::size_t videos = 0;
    for (const auto& entry : media_extensions()) {
        CHECK(seen.insert(entry.extension).second);
        (kind_of(entry.format) == MediaKind::photo ? photos : videos) += 1;
    }
    CHECK(photos == 17);
    CHECK(videos == 8);
}

TEST_CASE("la table porte exactement les extensions decidees")
{
    const std::set<std::string_view> photos{"jpg", "jpeg", "png", "heic", "heif", "tif",
                                            "tiff", "webp", "gif", "dng", "cr2", "cr3",
                                            "nef", "arw", "raf", "orf", "rw2"};
    const std::set<std::string_view> videos{"mp4", "mov", "m4v", "avi",
                                            "mkv", "mts", "m2ts", "3gp"};
    for (const auto& entry : media_extensions()) {
        const auto& expected = kind_of(entry.format) == MediaKind::photo ? photos : videos;
        INFO(entry.extension);
        CHECK(expected.contains(entry.extension));
    }
}

TEST_CASE("chaque extension de la table est reconnue quelle que soit la casse")
{
    for (const auto& entry : media_extensions()) {
        const std::string lower = "DCIM/100CANON/IMG_0001." + std::string(entry.extension);
        const std::string upper = "DCIM/100CANON/IMG_0001." + to_upper(entry.extension);
        // Une lettre sur deux en capitale : jPeG, Rw2, 3gP.
        std::string mixed = lower;
        const std::size_t start = mixed.size() - entry.extension.size();
        for (std::size_t i = start; i < mixed.size(); i += 2)
            mixed[i] = to_upper(std::string_view(&mixed[i], 1))[0];
        INFO(lower);
        CHECK(format_from_extension(lower) == entry.format);
        CHECK(format_from_extension(upper) == entry.format);
        CHECK(format_from_extension(mixed) == entry.format);
    }
}

TEST_CASE("les extensions synonymes donnent le meme format")
{
    CHECK(format_from_extension("a.jpg") == MediaFormat::jpeg);
    CHECK(format_from_extension("a.JPEG") == MediaFormat::jpeg);
    CHECK(format_from_extension("a.heic") == MediaFormat::heif);
    CHECK(format_from_extension("a.HEIF") == MediaFormat::heif);
    CHECK(format_from_extension("a.tif") == MediaFormat::tiff);
    CHECK(format_from_extension("a.tiff") == MediaFormat::tiff);
    CHECK(format_from_extension("00001.MTS") == MediaFormat::mpeg_ts);
    CHECK(format_from_extension("00001.m2ts") == MediaFormat::mpeg_ts);
}

TEST_CASE("seule compte l'extension du dernier element du chemin")
{
    CHECK(format_from_extension("2024.jpg/notes.txt") == std::nullopt);
    CHECK(format_from_extension("vacances.2024/IMG_0001.JPG") == MediaFormat::jpeg);
    CHECK(format_from_extension("photo.txt.jpg") == MediaFormat::jpeg);
    CHECK(format_from_extension("photo.jpg.txt") == std::nullopt);
    CHECK(format_from_extension("Camera/VID_20241231.mp4") == MediaFormat::mp4);
}

TEST_CASE("un nom sans extension reconnue n'est pas un media")
{
    const auto path = GENERATE(as<std::string_view>{},
                               "",
                               "photo",
                               "photo.",
                               "dossier/",
                               ".jpg",
                               "DCIM/.jpg",
                               "photo.jp",
                               "photo.jpgx",
                               "photo.jpeg2",
                               "photo. jpg",
                               "photo.txt",
                               "photo.mpg");
    INFO(path);
    CHECK(format_from_extension(path) == std::nullopt);
}

TEST_CASE("les fichiers accompagnants ne sont pas reconnus comme medias")
{
    const auto path = GENERATE(as<std::string_view>{},
                               "IMG_0001.xmp",
                               "IMG_0001.JPG.xmp",
                               "IMG_0001.AAE",
                               "GOPR0001.THM",
                               "GL010001.LRV",
                               "DJI_0001.SRT");
    INFO(path);
    CHECK(format_from_extension(path) == std::nullopt);
}

TEST_CASE("un fichier AppleDouble garde l'extension de son nom")
{
    // ._IMG_0001.JPG n'est pas une photo, mais c'est la signature qui le dira :
    // l'extension seule ne permet pas de le distinguer.
    CHECK(format_from_extension("DCIM/._IMG_0001.JPG") == MediaFormat::jpeg);
}

TEST_CASE("un nom en UTF-8 non ASCII est traite sans erreur")
{
    CHECK(format_from_extension("été/plage à Nice.JPG") == MediaFormat::jpeg);
    // « É » en UTF-8 : deux octets, qui ne doivent rien égaler.
    CHECK(format_from_extension("photo.\xC3\x89") == std::nullopt);
}
