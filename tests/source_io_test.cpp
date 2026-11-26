#include "core/source_io.hpp"

#include "core/directory_source.hpp"
#include "core/source.hpp"
#include "exif_samples.hpp"
#include "memory_source.hpp"
#include "test_files.hpp"

#include <exiv2/exiv2.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Noms de tests sans accents : voir version_test.cpp.

using test_support::jpeg_with_capture_date;
using test_support::MemorySource;
using test_support::TempDir;
using test_support::tiff_with_capture_date;
using test_support::utf8_path;
using test_support::write_file;
using transfertphoto::SourceIo;

namespace {

std::vector<std::uint8_t> pattern(std::size_t size)
{
    std::vector<std::uint8_t> content(size);
    for (std::size_t i = 0; i < size; ++i)
        content[i] = static_cast<std::uint8_t>((i * 7) % 251);
    return content;
}

std::string capture_date(transfertphoto::Source& source, const std::string& path, std::uint64_t size)
{
    auto image = Exiv2::ImageFactory::open(std::make_unique<SourceIo>(source, path, size));
    image->readMetadata();
    const auto& exif = image->exifData();
    const auto date = exif.findKey(Exiv2::ExifKey("Exif.Photo.DateTimeOriginal"));
    return date == exif.end() ? std::string() : date->toString();
}

std::string as_text(const std::vector<std::uint8_t>& content)
{
    return {content.begin(), content.end()};
}

} // namespace

TEST_CASE("SourceIo lit a travers les blocs comme un fichier")
{
    const auto content = pattern(3 * SourceIo::block_size + 100);
    MemorySource source;
    source.contents["a.jpg"] = content;
    SourceIo io(source, "a.jpg", content.size());
    REQUIRE(io.open() == 0);
    CHECK(io.isopen());
    CHECK(io.size() == content.size());

    SECTION("lecture a cheval sur deux blocs")
    {
        const std::size_t start = SourceIo::block_size - 6;
        REQUIRE(io.seek(static_cast<int64_t>(start), Exiv2::BasicIo::beg) == 0);
        std::vector<Exiv2::byte> buffer(20);
        CHECK(io.read(buffer.data(), buffer.size()) == 20);
        CHECK(std::equal(buffer.begin(), buffer.end(), content.begin() + start));
        CHECK(io.tell() == start + 20);
        CHECK(io.getb() == content[start + 20]);
        CHECK_FALSE(io.eof());
        CHECK(io.error() == 0);
    }
    SECTION("une grande lecture traverse plusieurs blocs")
    {
        const auto data = io.read(2 * SourceIo::block_size + 10);
        REQUIRE(data.size() == 2 * SourceIo::block_size + 10);
        CHECK(std::equal(data.c_data(), data.c_data() + data.size(), content.begin()));
    }
    SECTION("la fin du fichier arrete la lecture et se signale")
    {
        REQUIRE(io.seek(-4, Exiv2::BasicIo::end) == 0);
        std::vector<Exiv2::byte> buffer(10);
        CHECK(io.read(buffer.data(), buffer.size()) == 4);
        CHECK(io.eof());
        CHECK(io.getb() == EOF);
        CHECK(io.error() == 0);
    }
    SECTION("un deplacement hors du fichier echoue")
    {
        CHECK(io.seek(-1, Exiv2::BasicIo::beg) != 0);
        CHECK(io.seek(1, Exiv2::BasicIo::end) != 0);
        CHECK(io.seek(INT64_MAX, Exiv2::BasicIo::cur) != 0);
        REQUIRE(io.seek(0, Exiv2::BasicIo::end) == 0);
        CHECK(io.tell() == content.size());
    }
    SECTION("rien n'est lu au-dela d'un bloc a la fois")
    {
        std::vector<Exiv2::byte> buffer(content.size());
        CHECK(io.read(buffer.data(), buffer.size()) == content.size());
        CHECK(source.largest_request == SourceIo::block_size);
    }
    SECTION("toute ecriture echoue")
    {
        const Exiv2::byte data[2] = {1, 2};
        CHECK(io.write(data, 2) == 0);
        CHECK(io.putb(1) == EOF);
        CHECK_THROWS_AS(io.mmap(true), Exiv2::Error);
    }
}

TEST_CASE("SourceIo signale l'erreur de la source")
{
    MemorySource source;
    source.contents["a.jpg"] = pattern(100);
    source.failure = std::make_error_code(std::errc::io_error);
    SourceIo io(source, "a.jpg", 100);
    REQUIRE(io.open() == 0);

    std::vector<Exiv2::byte> buffer(10);
    CHECK(io.read(buffer.data(), buffer.size()) == 0);
    CHECK(io.error() != 0);
    CHECK(io.source_error() == std::errc::io_error);
    CHECK_THROWS_AS(io.mmap(), Exiv2::Error);
}

TEST_CASE("Exiv2 lit la date d'un JPEG au travers d'une source")
{
    const auto jpeg = jpeg_with_capture_date("2023:12:25 18:04:00");
    MemorySource source;
    source.contents["DCIM/IMG_0001.JPG"] = jpeg;

    CHECK(capture_date(source, "DCIM/IMG_0001.JPG", jpeg.size()) == "2023:12:25 18:04:00");
    // Un JPEG se lit par morceaux, jamais en entier.
    CHECK(source.largest_request <= SourceIo::block_size);
}

TEST_CASE("un TIFF d'une source non locale est lu en entier")
{
    const auto tiff = tiff_with_capture_date("2022:06:01 07:00:00");
    MemorySource source;
    source.contents["RAW/IMG_0002.TIF"] = tiff;

    CHECK(capture_date(source, "RAW/IMG_0002.TIF", tiff.size()) == "2022:06:01 07:00:00");
}

TEST_CASE("un TIFF local est projete en memoire au lieu d'etre lu")
{
    TempDir root;
    const auto on_disk = tiff_with_capture_date("2021:03:04 05:06:07");
    const auto in_source = tiff_with_capture_date("1999:09:09 09:09:09");
    REQUIRE(on_disk.size() == in_source.size());
    const auto file = root.path() / utf8_path("Été") / utf8_path("Noël.tif");
    write_file(file, as_text(on_disk));

    MemorySource source;
    source.contents["Été/Noël.tif"] = in_source;
    source.local_paths["Été/Noël.tif"] = file;

    CHECK(capture_date(source, "Été/Noël.tif", on_disk.size()) == "2021:03:04 05:06:07");
}

TEST_CASE("un TIFF local qui n'a plus sa taille n'est pas lu")
{
    TempDir root;
    const auto tiff = tiff_with_capture_date("2021:03:04 05:06:07");
    const auto file = root.path() / "court.tif";
    write_file(file, as_text(tiff));

    MemorySource source;
    auto announced = tiff;
    announced.resize(tiff.size() + 1000);
    source.contents["court.tif"] = announced;
    source.local_paths["court.tif"] = file;

    CHECK_THROWS_AS(capture_date(source, "court.tif", announced.size()), Exiv2::Error);
}

TEST_CASE("Exiv2 lit un dossier local par son adaptateur, chemin accentue compris")
{
    TempDir root;
    const auto jpeg = jpeg_with_capture_date("2020:02:29 12:00:00");
    const auto tiff = tiff_with_capture_date("2019:11:11 11:11:11");
    write_file(root.path() / utf8_path("Été") / utf8_path("Noël.jpg"), as_text(jpeg));
    write_file(root.path() / utf8_path("Été") / utf8_path("Noël.tif"), as_text(tiff));

    transfertphoto::DirectorySource source(root.path());
    CHECK(capture_date(source, "Été/Noël.jpg", jpeg.size()) == "2020:02:29 12:00:00");
    CHECK(capture_date(source, "Été/Noël.tif", tiff.size()) == "2019:11:11 11:11:11");
}
