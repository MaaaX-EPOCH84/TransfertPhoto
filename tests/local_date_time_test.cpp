#include "core/local_date_time.hpp"

#include <catch2/catch_test_macros.hpp>

// Noms de tests sans accents : voir version_test.cpp.

using transfertphoto::LocalDateTime;
using transfertphoto::parse_exif_date_time;
using transfertphoto::parse_exif_subsecond;

TEST_CASE("une date EXIF se lit")
{
    const auto date = parse_exif_date_time("2024:07:14 09:30:05");
    REQUIRE(date);
    CHECK(*date == LocalDateTime{2024, 7, 14, 9, 30, 5, 0});
    CHECK(to_string(*date) == "2024-07-14 09:30:05");
}

TEST_CASE("les variantes d'ecriture d'une date EXIF sont tolerees")
{
    const LocalDateTime expected{2024, 7, 14, 9, 30, 5, 0};
    CHECK(parse_exif_date_time("2024-07-14 09:30:05") == expected);
    CHECK(parse_exif_date_time("2024:07:14T09:30:05") == expected);
    CHECK(parse_exif_date_time("2024:07:14 09:30:05+02:00") == expected);
    CHECK(parse_exif_date_time(std::string_view("2024:07:14 09:30:05\0", 20)) == expected);
}

TEST_CASE("une date EXIF vide ou impossible ne donne rien")
{
    CHECK_FALSE(parse_exif_date_time(""));
    CHECK_FALSE(parse_exif_date_time("    :  :     :  :  "));
    CHECK_FALSE(parse_exif_date_time("0000:00:00 00:00:00"));
    CHECK_FALSE(parse_exif_date_time("2024:07:14"));
    CHECK_FALSE(parse_exif_date_time("2024:13:01 00:00:00"));
    CHECK_FALSE(parse_exif_date_time("2023:02:29 00:00:00"));
    CHECK_FALSE(parse_exif_date_time("2024:04:31 00:00:00"));
    CHECK_FALSE(parse_exif_date_time("2024:07:14 24:00:00"));
    CHECK_FALSE(parse_exif_date_time("2024:07:14 09:60:00"));
    CHECK_FALSE(parse_exif_date_time("2024:07-14 09:30:05"));
    CHECK_FALSE(parse_exif_date_time("2024/07/14 09:30:05"));
    CHECK_FALSE(parse_exif_date_time("2024:07:14 9:30:05 "));
}

TEST_CASE("le 29 fevrier n'existe que les annees bissextiles")
{
    CHECK(parse_exif_date_time("2024:02:29 00:00:00"));
    CHECK(parse_exif_date_time("2000:02:29 00:00:00"));
    CHECK_FALSE(parse_exif_date_time("1900:02:29 00:00:00"));
}

TEST_CASE("une fraction de seconde EXIF se lit en nanosecondes")
{
    CHECK(parse_exif_subsecond("5") == 500'000'000u);
    CHECK(parse_exif_subsecond("045") == 45'000'000u);
    CHECK(parse_exif_subsecond("123456789123") == 123'456'789u);
    CHECK(parse_exif_subsecond("12 ") == 120'000'000u);
    CHECK_FALSE(parse_exif_subsecond(""));
    CHECK_FALSE(parse_exif_subsecond("   "));
    CHECK_FALSE(parse_exif_subsecond("1a"));
}

TEST_CASE("les dates se rangent dans l'ordre, fraction de seconde comprise")
{
    const LocalDateTime first{2024, 7, 14, 9, 30, 5, 100};
    const LocalDateTime second{2024, 7, 14, 9, 30, 5, 200};
    const LocalDateTime next_day{2024, 7, 15, 0, 0, 0, 0};
    CHECK(first < second);
    CHECK(second < next_day);
}
