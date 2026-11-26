#include "core/photo_date.hpp"

#include "core/local_date_time.hpp"
#include "exif_samples.hpp"
#include "memory_source.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

// Noms de tests sans accents : voir version_test.cpp.

using namespace test_support;
using transfertphoto::LocalDateTime;
using transfertphoto::read_photo_date;

namespace {

transfertphoto::PhotoDateRead read(const std::vector<std::uint8_t>& content)
{
    MemorySource source;
    source.contents["photo"] = content;
    return read_photo_date(source, "photo", content.size());
}

const LocalDateTime july{2024, 7, 14, 9, 30, 5, 0};

} // namespace

TEST_CASE("la date de prise de vue se lit dans chaque format fabrique")
{
    const auto tiff = tiff_with_capture_date("2024:07:14 09:30:05");

    SECTION("JPEG") { CHECK(read(jpeg_with_tiff(tiff)).date == july); }
    SECTION("TIFF") { CHECK(read(tiff).date == july); }
    SECTION("PNG") { CHECK(read(png_with_tiff(tiff)).date == july); }
    SECTION("WebP") { CHECK(read(webp_with_tiff(tiff)).date == july); }
}

TEST_CASE("DateTimeDigitized sert a defaut de DateTimeOriginal")
{
    const auto only_digitized = tiff_with_tags({}, {{tag_date_time_digitized, "2024:07:14 09:30:05"}});
    CHECK(read(jpeg_with_tiff(only_digitized)).date == july);

    const auto both = tiff_with_tags({}, {{tag_date_time_original, "2024:07:14 09:30:05"},
                                          {tag_date_time_digitized, "2025:01:01 00:00:00"}});
    CHECK(read(jpeg_with_tiff(both)).date == july);

    // Un DateTimeOriginal illisible laisse la place au suivant.
    const auto blank_original = tiff_with_tags({}, {{tag_date_time_original, "    :  :     :  :  "},
                                                    {tag_date_time_digitized, "2024:07:14 09:30:05"}});
    CHECK(read(jpeg_with_tiff(blank_original)).date == july);
}

TEST_CASE("la fraction de seconde suit la date retenue")
{
    const auto original = tiff_with_tags({}, {{tag_date_time_original, "2024:07:14 09:30:05"},
                                              {tag_subsec_original, "045"},
                                              {tag_subsec_digitized, "9"}});
    const auto date = read(jpeg_with_tiff(original)).date;
    REQUIRE(date);
    CHECK(date->nanosecond == 45'000'000u);

    const auto digitized = tiff_with_tags({}, {{tag_date_time_digitized, "2024:07:14 09:30:05"},
                                               {tag_subsec_original, "045"},
                                               {tag_subsec_digitized, "9"}});
    const auto other = read(jpeg_with_tiff(digitized)).date;
    REQUIRE(other);
    CHECK(other->nanosecond == 900'000'000u);
}

TEST_CASE("le DateTime de l'IFD0 seul ne date pas la photo")
{
    const auto edited = tiff_with_tags({{tag_date_time, "2024:07:14 09:30:05"}}, {});
    const auto result = read(jpeg_with_tiff(edited));
    CHECK_FALSE(result.date);
    CHECK_FALSE(result.source_error);
}

TEST_CASE("une photo sans EXIF ou a l'EXIF abime n'a pas de date, sans erreur")
{
    SECTION("JPEG sans aucune metadonnee")
    {
        const auto result = read(bare_jpeg());
        CHECK_FALSE(result.date);
        CHECK_FALSE(result.source_error);
    }
    SECTION("pointeur vers l'IFD EXIF hors du fichier")
    {
        auto tiff = tiff_with_capture_date("2024:07:14 09:30:05");
        tiff[8 + 2 + 8] = 0xFF; // poids fort du décalage de l'IFD EXIF
        tiff[8 + 2 + 9] = 0xFF;
        const auto result = read(jpeg_with_tiff(tiff));
        CHECK_FALSE(result.date);
        CHECK_FALSE(result.source_error);
    }
    SECTION("TIFF tronque au milieu de son IFD")
    {
        auto tiff = tiff_with_capture_date("2024:07:14 09:30:05");
        tiff.resize(20);
        CHECK_FALSE(read(tiff).date);
        CHECK_FALSE(read(jpeg_with_tiff(tiff)).date);
    }
    SECTION("nombre d'entrees demesure")
    {
        auto tiff = tiff_with_capture_date("2024:07:14 09:30:05");
        tiff[8] = 0xFF;
        tiff[9] = 0xFF;
        CHECK_FALSE(read(tiff).date);
    }
    SECTION("fichier vide")
    {
        const auto result = read({});
        CHECK_FALSE(result.date);
        CHECK_FALSE(result.source_error);
    }
    SECTION("contenu qui n'est pas une image")
    {
        const std::string text = "ceci n'est pas une photo";
        CHECK_FALSE(read({text.begin(), text.end()}).date);
    }
}

TEST_CASE("une source qui lache se signale")
{
    const auto jpeg = jpeg_with_capture_date("2024:07:14 09:30:05");
    MemorySource source;
    source.contents["photo"] = jpeg;
    source.failure = std::make_error_code(std::errc::io_error);

    const auto result = read_photo_date(source, "photo", jpeg.size());
    CHECK_FALSE(result.date);
    CHECK(result.source_error == std::errc::io_error);
}
