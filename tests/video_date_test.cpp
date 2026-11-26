#include "core/video_date.hpp"

#include "core/local_date_time.hpp"
#include "memory_source.hpp"
#include "video_samples.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <optional>
#include <string>

// Noms de tests sans accents : voir version_test.cpp.

using namespace test_support;
using transfertphoto::LocalDateTime;
using transfertphoto::local_from_unix_seconds;
using transfertphoto::MediaFormat;
using transfertphoto::read_video_date;
using transfertphoto::utc_from_unix_seconds;
using transfertphoto::VideoDateRead;

namespace {

// 2023-12-25 17:04:00 UTC.
constexpr std::int64_t christmas_utc = 1'703'523'840;
constexpr std::int64_t from_1904 = 2'082'844'800;
constexpr std::int64_t to_2001 = 978'307'200;

VideoDateRead read(const VideoBytes& content, MediaFormat format, MemorySource* keep = nullptr)
{
    MemorySource local;
    MemorySource& source = keep != nullptr ? *keep : local;
    source.contents["video"] = content;
    return read_video_date(source, "video", content.size(), format);
}

// L'heure locale de la machine pour cet instant, par la bibliothèque C.
LocalDateTime machine_local(std::int64_t seconds)
{
    const auto instant = static_cast<std::time_t>(seconds);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &instant);
#else
    localtime_r(&instant, &local);
#endif
    return {local.tm_year + 1900,
            static_cast<unsigned>(local.tm_mon + 1),
            static_cast<unsigned>(local.tm_mday),
            static_cast<unsigned>(local.tm_hour),
            static_cast<unsigned>(local.tm_min),
            static_cast<unsigned>(local.tm_sec),
            0};
}

} // namespace

TEST_CASE("un instant UTC se met au calendrier")
{
    CHECK(utc_from_unix_seconds(0) == LocalDateTime{1970, 1, 1, 0, 0, 0, 0});
    CHECK(utc_from_unix_seconds(christmas_utc) == LocalDateTime{2023, 12, 25, 17, 4, 0, 0});
    CHECK(utc_from_unix_seconds(951'782'400) == LocalDateTime{2000, 2, 29, 0, 0, 0, 0});
    CHECK(utc_from_unix_seconds(-1) == LocalDateTime{1969, 12, 31, 23, 59, 59, 0});
    CHECK(utc_from_unix_seconds(-from_1904) == LocalDateTime{1904, 1, 1, 0, 0, 0, 0});
    CHECK(utc_from_unix_seconds(to_2001) == LocalDateTime{2001, 1, 1, 0, 0, 0, 0});
}

TEST_CASE("l'heure locale est celle de la machine pour le meme instant")
{
    CHECK(local_from_unix_seconds(christmas_utc) == machine_local(christmas_utc));
    CHECK(local_from_unix_seconds(1'688'212'800) == machine_local(1'688'212'800)); // en été
}

#ifndef _WIN32
TEST_CASE("l'heure locale suit le fuseau et l'heure d'ete")
{
    // Fuseau de Paris décrit à la manière POSIX : pas besoin de la base des
    // fuseaux du système.
    const char* saved = std::getenv("TZ");
    const std::optional<std::string> previous = saved ? std::optional<std::string>(saved) : std::nullopt;
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();

    CHECK(local_from_unix_seconds(1'673'784'000) == LocalDateTime{2023, 1, 15, 13, 0, 0, 0}); // UTC+1
    CHECK(local_from_unix_seconds(1'688'212'800) == LocalDateTime{2023, 7, 1, 14, 0, 0, 0});  // UTC+2

    if (previous)
        setenv("TZ", previous->c_str(), 1);
    else
        unsetenv("TZ");
    tzset();
}
#endif

TEST_CASE("la date d'un MP4 est le creation_time de mvhd, a l'heure locale")
{
    const auto expected = local_from_unix_seconds(christmas_utc);
    const std::uint64_t created = christmas_utc + from_1904;

    SECTION("moov en tete")
    {
        CHECK(read(mp4({.mvhd_created = created}), MediaFormat::mp4).date == expected);
    }
    SECTION("mvhd en version 1, sur 64 bits")
    {
        CHECK(read(mp4({.mvhd_created = created, .mvhd_version = 1}), MediaFormat::mp4).date == expected);
    }
    SECTION("moov en fin, derriere un gros mdat qui n'est pas lu")
    {
        MemorySource source;
        const auto file = mp4({.mvhd_created = created, .moov_at_end = true, .mdat_size = 3'000'000});
        CHECK(read(file, MediaFormat::mp4, &source).date == expected);
        CHECK(source.largest_request < 4096);
    }
    SECTION("3GP et M4V se lisent de meme")
    {
        CHECK(read(mp4({.mvhd_created = created, .brand = "3gp4"}), MediaFormat::three_gp).date == expected);
        CHECK(read(mp4({.mvhd_created = created, .brand = "M4V "}), MediaFormat::m4v).date == expected);
    }
}

TEST_CASE("la cle Apple passe avant mvhd et garde l'heure du lieu")
{
    // mvhd porte un autre instant, d'une autre année : quel que soit le fuseau
    // de la machine, sa conversion ne peut pas retomber sur l'heure de la clé.
    constexpr std::int64_t june_2022 = 1'654'041'600; // 2022-06-01 00:00:00 UTC
    const std::uint64_t created = june_2022 + from_1904;
    const LocalDateTime paris{2023, 12, 25, 18, 4, 0, 0};
    const auto meta = mdta_meta({{"com.apple.quicktime.make", "Apple"},
                                 {"com.apple.quicktime.creationdate", "2023-12-25T18:04:00+0100"}});

    SECTION("meta de QuickTime")
    {
        CHECK(read(mp4({.mvhd_created = created, .meta = meta, .brand = "qt  "}), MediaFormat::mov).date
              == paris);
    }
    SECTION("meta a la norme ISO, avec version et drapeaux")
    {
        const auto iso = mdta_meta({{"com.apple.quicktime.creationdate", "2023-12-25T18:04:00+0100"}}, true);
        CHECK(read(mp4({.mvhd_created = created, .meta = iso}), MediaFormat::mov).date == paris);
    }
    SECTION("meta rangee dans udta, comme l'ecrit ffmpeg")
    {
        const auto iso = mdta_meta({{"com.apple.quicktime.creationdate", "2023-12-25T18:04:00+0100"}}, true);
        CHECK(read(mp4({.mvhd_created = created, .meta = iso, .meta_in_udta = true}), MediaFormat::mov).date
              == paris);
    }
    SECTION("sans mvhd")
    {
        CHECK(read(mp4({.meta = meta}), MediaFormat::mov).date == paris);
    }
    SECTION("une cle Apple illisible laisse la place a mvhd")
    {
        const auto broken = mdta_meta({{"com.apple.quicktime.creationdate", "hier soir"}});
        CHECK(read(mp4({.mvhd_created = created, .meta = broken}), MediaFormat::mov).date
              == local_from_unix_seconds(june_2022));
    }
}

TEST_CASE("une date nulle de mvhd vaut absence")
{
    const auto result = read(mp4({.mvhd_created = 0}), MediaFormat::mp4);
    CHECK_FALSE(result.date);
    CHECK_FALSE(result.source_error);
    CHECK_FALSE(read(mp4({}), MediaFormat::mp4).date);
}

TEST_CASE("la date d'un AVI est son IDIT, sous les ecritures des appareils")
{
    const LocalDateTime evening{2023, 12, 25, 18, 4, 0, 0};
    CHECK(read(avi(std::string("Mon Dec 25 18:04:00 2023\n", 26)), MediaFormat::avi).date == evening);
    CHECK(read(avi(std::string("MON DEC 25 18:04:00 2023\n\0", 26)), MediaFormat::avi).date == evening);
    CHECK(read(avi(std::string("2002-12-16  15:35:01\0\0", 22)), MediaFormat::avi).date
          == LocalDateTime{2002, 12, 16, 15, 35, 1, 0});
    CHECK(read(avi(std::string("2005/11/28/ 09:19")), MediaFormat::avi).date
          == LocalDateTime{2005, 11, 28, 9, 19, 0, 0});
    CHECK(read(avi(std::string("2001/ 1/27  1:42PM")), MediaFormat::avi).date
          == LocalDateTime{2001, 1, 27, 13, 42, 0, 0});
}

TEST_CASE("un AVI sans IDIT lisible n'a pas de date")
{
    CHECK_FALSE(read(avi(std::nullopt), MediaFormat::avi).date);
    CHECK_FALSE(read(avi(std::string("pas de date")), MediaFormat::avi).date);
    CHECK_FALSE(read(avi(std::string("Mon Feb 30 18:04:00 2023")), MediaFormat::avi).date);
}

TEST_CASE("la date d'un MKV est son DateUTC, a l'heure locale")
{
    const std::int64_t ns = (christmas_utc - to_2001) * 1'000'000'000 + 250'000'000;
    const auto expected = local_from_unix_seconds(christmas_utc);
    CHECK(read(mkv({.date_utc_ns = ns}), MediaFormat::mkv).date == expected);
    CHECK(read(mkv({.date_utc_ns = ns, .unknown_segment_size = true}), MediaFormat::mkv).date == expected);
    // Avant 2001 : un nombre négatif, arrondi vers le passé, pas vers zéro.
    const std::int64_t before = (951'782'400 - to_2001) * 1'000'000'000 + 250'000'000;
    CHECK(read(mkv({.date_utc_ns = before}), MediaFormat::mkv).date == local_from_unix_seconds(951'782'400));
}

TEST_CASE("un MKV sans DateUTC ou a DateUTC nul n'a pas de date")
{
    CHECK_FALSE(read(mkv({}), MediaFormat::mkv).date);
    CHECK_FALSE(read(mkv({.date_utc_ns = 0}), MediaFormat::mkv).date);
}

TEST_CASE("la date d'un MTS est son MDPM")
{
    const MdpmDate date{2023, 12, 25, 18, 4, 30};
    const LocalDateTime expected{2023, 12, 25, 18, 4, 30, 0};

    SECTION("paquets de 188 octets")
    {
        CHECK(read(mts({.date = date}), MediaFormat::mpeg_ts).date == expected);
    }
    SECTION("M2TS, paquets de 192 octets")
    {
        CHECK(read(mts({.date = date, .packet_size = 192}), MediaFormat::mpeg_ts).date == expected);
    }
    SECTION("SEI a cheval sur deux paquets, entrecoupes d'un paquet de son")
    {
        for (std::size_t filler = 140; filler < 184; filler += 3) {
            INFO("remplissage " << filler);
            CHECK(read(mts({.date = date, .filler_before_sei = filler}), MediaFormat::mpeg_ts).date == expected);
        }
    }
    SECTION("octets d'emulation glisses apres deux zeros")
    {
        // Minute et seconde à zéro, suivies d'un octet faible : le codeur
        // insère 0x03, qu'il faut retirer.
        const MdpmDate midnight{2024, 1, 1, 0, 0, 0};
        CHECK(read(mts({.date = midnight}), MediaFormat::mpeg_ts).date == LocalDateTime{2024, 1, 1, 0, 0, 0, 0});
    }
}

TEST_CASE("un MTS sans MDPM n'a pas de date")
{
    CHECK_FALSE(read(mts({}), MediaFormat::mpeg_ts).date);
    CHECK_FALSE(read(mts({.date = MdpmDate{2023, 13, 1, 0, 0, 0}}), MediaFormat::mpeg_ts).date);
}

TEST_CASE("un conteneur abime n'a pas de date et ne fait rien tomber")
{
    const std::uint64_t created = christmas_utc + from_1904;

    SECTION("MP4 tronque au milieu de moov")
    {
        auto file = mp4({.mvhd_created = created});
        file.resize(40);
        CHECK_FALSE(read(file, MediaFormat::mp4).date);
    }
    SECTION("boites de tailles absurdes")
    {
        VideoBytes file{0xFF, 0xFF, 0xFF, 0xFF, 'm', 'o', 'o', 'v', 0, 0};
        CHECK_FALSE(read(file, MediaFormat::mp4).date);
        VideoBytes large{0, 0, 0, 1, 'm', 'o', 'o', 'v', 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        CHECK_FALSE(read(large, MediaFormat::mp4).date);
        VideoBytes small{0, 0, 0, 4, 'f', 't', 'y', 'p'};
        CHECK_FALSE(read(small, MediaFormat::mp4).date);
    }
    SECTION("meta aux cles et elements incoherents")
    {
        auto meta = mdta_meta({{"com.apple.quicktime.creationdate", "2023-12-25T18:04:00+0100"}});
        // Taille de la première clé gonflée.
        const std::size_t keys = 8 + (8 + 25) + 8 + 4 + 4;
        meta[keys] = 0x7F;
        CHECK_FALSE(read(mp4({.meta = meta}), MediaFormat::mov).date);
    }
    SECTION("AVI au bloc demesure")
    {
        auto file = avi(std::string("Mon Dec 25 18:04:00 2023"));
        file[16] = 0xFF;
        file[17] = 0xFF;
        file[18] = 0xFF;
        file[19] = 0x7F;
        CHECK_FALSE(read(file, MediaFormat::avi).date);
    }
    SECTION("MKV aux entiers de longueur nulle")
    {
        VideoBytes file{0x1A, 0x45, 0xDF, 0xA3, 0x00, 0x00};
        CHECK_FALSE(read(file, MediaFormat::mkv).date);
        // Coupé au milieu du bloc Info, qui suit l'en-tête EBML (30 octets),
        // celui du Segment (12) et le SeekHead (32).
        auto truncated = mkv({.date_utc_ns = 1});
        truncated.resize(74 + 14);
        CHECK_FALSE(read(truncated, MediaFormat::mkv).date);
    }
    SECTION("MTS sans synchronisation, ou vide")
    {
        CHECK_FALSE(read(VideoBytes(1000, 0x47), MediaFormat::mpeg_ts).date);
        CHECK_FALSE(read(VideoBytes(1000, 0x00), MediaFormat::mpeg_ts).date);
        CHECK_FALSE(read({}, MediaFormat::mpeg_ts).date);
    }
    SECTION("format qui n'est pas une video")
    {
        CHECK_FALSE(read(mp4({.mvhd_created = created}), MediaFormat::jpeg).date);
    }
}

TEST_CASE("une source qui lache se signale pour une video")
{
    MemorySource source;
    source.failure = std::make_error_code(std::errc::io_error);
    const auto file = mp4({.mvhd_created = christmas_utc + from_1904});
    source.contents["video"] = file;
    const auto result = read_video_date(source, "video", file.size(), MediaFormat::mp4);
    CHECK_FALSE(result.date);
    CHECK(result.source_error == std::errc::io_error);
}
