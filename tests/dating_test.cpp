#include "core/dating.hpp"

#include "core/local_date_time.hpp"
#include "exif_samples.hpp"
#include "memory_source.hpp"
#include "video_samples.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <stop_token>
#include <string>
#include <vector>

// Noms de tests sans accents : voir version_test.cpp.

using namespace test_support;
using transfertphoto::CompanionFile;
using transfertphoto::CompanionKind;
using transfertphoto::date_media;
using transfertphoto::DateOrigin;
using transfertphoto::Dating;
using transfertphoto::DatingProgress;
using transfertphoto::Inventory;
using transfertphoto::LocalDateTime;
using transfertphoto::local_from_unix_seconds;
using transfertphoto::MediaFormat;
using transfertphoto::MediaItem;
using transfertphoto::ScanOutcome;
using transfertphoto::SignatureVerdict;
using transfertphoto::SourceEntry;

namespace {

// 2021-05-06 07:08:09 UTC, la date de modification de tous les fichiers.
constexpr std::int64_t modified_utc = 1'620'284'889;

SourceEntry entry(const std::string& path, std::size_t size)
{
    SourceEntry result;
    result.relative_path = path;
    result.size = size;
    result.modified = std::chrono::system_clock::time_point(std::chrono::seconds(modified_utc));
    return result;
}

// Une source et l'inventaire qui la décrit, construits ensemble.
struct Fixture {
    MemorySource source;
    Inventory inventory;

    MediaItem& add(const std::string& path, const std::vector<std::uint8_t>& content, MediaFormat format)
    {
        source.contents[path] = content;
        MediaItem item;
        item.entry = entry(path, content.size());
        item.format = format;
        item.signature.verdict = SignatureVerdict::matches;
        item.signature.detected = format;
        inventory.media.push_back(item);
        return inventory.media.back();
    }

    Dating date(std::stop_token stop = {}) { return date_media(source, inventory, stop); }
};

const LocalDateTime exif_date{2023, 12, 25, 18, 4, 0, 0};
const LocalDateTime name_date{2022, 1, 2, 3, 4, 5, 0};

std::vector<DateOrigin> origins(const transfertphoto::MediaDates& dates)
{
    std::vector<DateOrigin> result;
    for (const auto& candidate : dates.candidates)
        result.push_back(candidate.origin);
    return result;
}

} // namespace

TEST_CASE("la date retenue suit l'ordre de repli, toutes les dates gardees")
{
    Fixture fixture;
    const auto with_exif = jpeg_with_capture_date("2023:12:25 18:04:00");
    fixture.add("IMG_20220102_030405.jpg", with_exif, MediaFormat::jpeg);
    fixture.add("sans_exif/IMG_20220102_030405.jpg", bare_jpeg(), MediaFormat::jpeg);
    fixture.add("sans_exif/DSC_0001.JPG", bare_jpeg(), MediaFormat::jpeg);
    fixture.add("WhatsApp/IMG-20220102-WA0001.jpg", bare_jpeg(), MediaFormat::jpeg);

    const auto dating = fixture.date();
    REQUIRE(dating.outcome == ScanOutcome::completed);
    REQUIRE(dating.media.size() == 4);
    const auto modified = local_from_unix_seconds(modified_utc);

    SECTION("metadonnees, puis nom, puis modification")
    {
        const auto& dates = dating.media[0];
        CHECK(origins(dates) == std::vector{DateOrigin::metadata, DateOrigin::file_name, DateOrigin::modification});
        CHECK(dates.retained().value == exif_date);
        CHECK(dates.candidates[1].value == name_date);
        CHECK(dates.candidates[2].value == modified);
    }
    SECTION("sans metadonnees, le nom")
    {
        const auto& dates = dating.media[1];
        CHECK(origins(dates) == std::vector{DateOrigin::file_name, DateOrigin::modification});
        CHECK(dates.retained().value == name_date);
        CHECK(dates.retained().has_time);
    }
    SECTION("ni metadonnees ni nom, la modification a l'heure locale")
    {
        const auto& dates = dating.media[2];
        CHECK(origins(dates) == std::vector{DateOrigin::modification});
        CHECK(dates.retained().value == modified);
    }
    SECTION("un nom qui ne donne que le jour le dit")
    {
        const auto& dates = dating.media[3];
        CHECK(dates.retained().origin == DateOrigin::file_name);
        CHECK_FALSE(dates.retained().has_time);
        CHECK(dates.retained().value == LocalDateTime{2022, 1, 2, 0, 0, 0, 0});
    }
}

TEST_CASE("une video est datee par son conteneur")
{
    Fixture fixture;
    constexpr std::int64_t christmas = 1'703'523'840;
    fixture.add("VID_0001.mp4", mp4({.mvhd_created = christmas + 2'082'844'800}), MediaFormat::mp4);
    const auto dating = fixture.date();
    REQUIRE(dating.media.size() == 1);
    CHECK(dating.media[0].retained().origin == DateOrigin::metadata);
    CHECK(dating.media[0].retained().value == local_from_unix_seconds(christmas));
}

TEST_CASE("le contenu decide du lecteur quand il contredit l'extension")
{
    Fixture fixture;
    constexpr std::int64_t christmas = 1'703'523'840;
    // Une vidéo MP4 nommée .avi, que la signature a démasquée.
    auto& video = fixture.add("clip.avi", mp4({.mvhd_created = christmas + 2'082'844'800}), MediaFormat::avi);
    video.signature.verdict = SignatureVerdict::mismatch;
    video.signature.detected = MediaFormat::mp4;
    // Une photo JPEG nommée .png.
    auto& photo = fixture.add("photo.png", jpeg_with_capture_date("2023:12:25 18:04:00"), MediaFormat::png);
    photo.signature.verdict = SignatureVerdict::mismatch;
    photo.signature.detected = MediaFormat::jpeg;

    const auto dating = fixture.date();
    REQUIRE(dating.media.size() == 2);
    CHECK(dating.media[0].retained().origin == DateOrigin::metadata);
    CHECK(dating.media[0].retained().value == local_from_unix_seconds(christmas));
    CHECK(dating.media[1].retained().value == exif_date);
}

TEST_CASE("les accompagnants prennent la date de leur porteur sans etre lus")
{
    Fixture fixture;
    auto& photo = fixture.add("IMG_0001.HEIC", jpeg_with_capture_date("2023:12:25 18:04:00"), MediaFormat::heif);
    for (const auto& [path, kind] : {std::pair{"IMG_0001.MOV", CompanionKind::live_video},
                                     std::pair{"IMG_0001.AAE", CompanionKind::aae},
                                     std::pair{"IMG_0001.xmp", CompanionKind::xmp}}) {
        fixture.source.contents[path] = jpeg_with_capture_date("1999:01:01 00:00:00");
        photo.companions.push_back(CompanionFile{entry(path, 10), kind});
    }

    const auto dating = fixture.date();
    REQUIRE(dating.media.size() == 1);
    CHECK(dating.media[0].retained().value == exif_date);
    CHECK(fixture.source.reads["IMG_0001.MOV"] == 0);
    CHECK(fixture.source.reads["IMG_0001.AAE"] == 0);
    CHECK(fixture.source.reads["IMG_0001.xmp"] == 0);
}

TEST_CASE("la progression est remontee fichier par fichier")
{
    Fixture fixture;
    for (int i = 0; i < 3; ++i)
        fixture.add("IMG_000" + std::to_string(i) + ".jpg", bare_jpeg(), MediaFormat::jpeg);

    std::vector<DatingProgress> seen;
    const auto dating = date_media(fixture.source, fixture.inventory, {},
                                   [&](const DatingProgress& progress) { seen.push_back(progress); });
    CHECK(dating.outcome == ScanOutcome::completed);
    REQUIRE(seen.size() == 3);
    for (std::size_t i = 0; i < seen.size(); ++i) {
        CHECK(seen[i].done == i + 1);
        CHECK(seen[i].total == 3);
    }
}

TEST_CASE("une datation interrompue ne rend rien")
{
    Fixture fixture;
    for (int i = 0; i < 3; ++i)
        fixture.add("IMG_000" + std::to_string(i) + ".jpg", bare_jpeg(), MediaFormat::jpeg);

    std::stop_source stop;
    const auto dating = date_media(fixture.source, fixture.inventory, stop.get_token(),
                                   [&](const DatingProgress& progress) {
                                       if (progress.done == 1)
                                           stop.request_stop();
                                   });
    CHECK(dating.outcome == ScanOutcome::interrupted);
    CHECK(dating.media.empty());
    CHECK(fixture.source.reads["IMG_0002.jpg"] == 0);
}

TEST_CASE("une source qui disparait arrete la datation")
{
    Fixture fixture;
    fixture.add("IMG_0001.jpg", bare_jpeg(), MediaFormat::jpeg);
    fixture.add("IMG_0002.jpg", bare_jpeg(), MediaFormat::jpeg);
    fixture.add("IMG_0003.jpg", bare_jpeg(), MediaFormat::jpeg);
    fixture.source.failing.insert("IMG_0002.jpg");
    fixture.source.vanishes_on_failure = true;

    const auto dating = fixture.date();
    CHECK(dating.outcome == ScanOutcome::source_unavailable);
    CHECK(dating.media.empty());
}

TEST_CASE("un fichier illisible d'une source presente est date autrement et note")
{
    Fixture fixture;
    fixture.add("IMG_20220102_030405.jpg", jpeg_with_capture_date("2023:12:25 18:04:00"), MediaFormat::jpeg);
    fixture.add("IMG_0002.jpg", bare_jpeg(), MediaFormat::jpeg);
    fixture.source.failing.insert("IMG_20220102_030405.jpg");

    const auto dating = fixture.date();
    CHECK(dating.outcome == ScanOutcome::completed);
    REQUIRE(dating.media.size() == 2);
    CHECK(dating.media[0].retained().origin == DateOrigin::file_name);
    CHECK(dating.media[0].retained().value == name_date);
    REQUIRE(dating.errors.size() == 1);
    CHECK(dating.errors[0].relative_path == "IMG_20220102_030405.jpg");
    CHECK(dating.errors[0].error == std::errc::io_error);
}
