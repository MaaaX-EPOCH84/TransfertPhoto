#include "core/inventory.hpp"

#include "core/directory_source.hpp"
#include "test_files.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <map>
#include <set>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Noms de tests sans accents : voir version_test.cpp.
//
// Attention aux échappements dans les chaînes : « \x18f » serait lu comme un
// seul octet. Je coupe la chaîne après chaque \x suivi d'un chiffre hexadécimal.

using namespace std::string_view_literals;
using test_support::TempDir;
using test_support::utf8_path;
using test_support::write_file;
using transfertphoto::CompanionKind;
using transfertphoto::DirectorySource;
using transfertphoto::Inventory;
using transfertphoto::InventoryProgress;
using transfertphoto::MediaFormat;
using transfertphoto::MediaItem;
using transfertphoto::ScanOutcome;
using transfertphoto::SignatureVerdict;
using transfertphoto::SourceEntry;
using transfertphoto::take_inventory;

namespace {

// Débuts de fichiers réalistes, assez pour la signature.
constexpr auto jpeg = "\xFF\xD8\xFF\xE1\0\x18" "Exif\0\0II*\0"sv;
constexpr auto heic = "\0\0\0\x18" "ftypheic\0\0\0\0mif1heic"sv;
constexpr auto cr3 = "\0\0\0\x18" "ftypcrx \0\0\0\x01" "crx isom"sv;
constexpr auto mov = "\0\0\0\x14" "ftypqt  \0\0\0\0qt  "sv;
constexpr auto mp4 = "\0\0\0\x20" "ftypisom\0\0\x02\0isomiso2avc1mp41"sv;
constexpr auto webp = "RIFF\x24\0\0\0WEBPVP8 "sv;
constexpr auto mkv = "\x1A\x45\xDF\xA3\x01\0\0\0"sv;
constexpr auto apple_double = "\0\x05\x16\x07\0\x02\0\0Mac OS X        "sv;
constexpr auto xml = "<?xml version=\"1.0\"?>"sv;

std::vector<std::string> paths_of(const std::vector<SourceEntry>& entries)
{
    std::vector<std::string> paths;
    for (const auto& entry : entries)
        paths.push_back(entry.relative_path);
    return paths;
}

std::vector<std::string> media_paths(const Inventory& inventory)
{
    std::vector<std::string> paths;
    for (const auto& item : inventory.media)
        paths.push_back(item.entry.relative_path);
    return paths;
}

const MediaItem& item(const Inventory& inventory, std::string_view path)
{
    const auto found = std::ranges::find(inventory.media, path, [](const MediaItem& m) -> std::string_view {
        return m.entry.relative_path;
    });
    REQUIRE(found != inventory.media.end());
    return *found;
}

std::vector<std::string> companion_paths(const MediaItem& media)
{
    std::vector<std::string> paths;
    for (const auto& companion : media.companions)
        paths.push_back(companion.entry.relative_path);
    return paths;
}

Inventory inventory_of(const TempDir& root)
{
    DirectorySource source(root.path());
    return take_inventory(source, {});
}

// Source entièrement décrite par le test : ce qu'elle contient, ce qu'elle
// ne sait pas lire, et si elle disparaît à la première lecture.
class ScriptedSource final : public transfertphoto::Source {
public:
    std::vector<SourceEntry> entries;
    std::map<std::string, std::string> contents;
    std::set<std::string> unreadable;
    bool vanishes_on_read = false;

    void add(std::string path, std::string_view content, bool hidden = false)
    {
        SourceEntry entry;
        entry.relative_path = path;
        entry.size = content.size();
        entry.hidden = hidden;
        entries.push_back(entry);
        contents[std::move(path)] = std::string(content);
    }

    std::string display_name() const override { return "essai"; }
    bool is_available() const override { return available_; }

    transfertphoto::ScanResult scan(const std::function<void(const SourceEntry&)>& on_entry,
                                    std::stop_token stop) override
    {
        transfertphoto::ScanResult result;
        // Dans le désordre, comme un vrai parcours peut le faire.
        for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
            if (stop.stop_requested()) {
                result.outcome = ScanOutcome::interrupted;
                return result;
            }
            on_entry(*it);
        }
        return result;
    }

    std::size_t read_head(std::string_view relative_path, std::span<std::byte> buffer,
                          std::error_code& error) override
    {
        error.clear();
        if (vanishes_on_read)
            available_ = false;
        const std::string path(relative_path);
        if (!available_ || unreadable.contains(path)) {
            error = std::make_error_code(std::errc::io_error);
            return 0;
        }
        const auto& content = contents.at(path);
        const auto count = std::min(buffer.size(), content.size());
        for (std::size_t i = 0; i < count; ++i)
            buffer[i] = static_cast<std::byte>(content[i]);
        return count;
    }

private:
    bool available_ = true;
};

} // namespace

TEST_CASE("inventaire d'une carte d'appareil photo")
{
    TempDir root;
    const auto& card = root.path();
    write_file(card / "DCIM" / "100CANON" / "IMG_0001.CR3", cr3);
    write_file(card / "DCIM" / "100CANON" / "IMG_0001.JPG", jpeg);
    write_file(card / "DCIM" / "100CANON" / "MVI_0002.MP4", mp4);
    write_file(card / "DCIM" / "100CANON" / "MVI_0002.THM", jpeg);
    write_file(card / "DCIM" / "100CANON" / "IMG_0003.JPG", ""); // copie avortée
    write_file(card / "DCIM" / "100CANON" / "._IMG_0001.JPG", apple_double);
    write_file(card / "MISC" / "AUTPRINT.MRK", "[HDR]");
    write_file(card / "System Volume Information" / "IndexerVolumeGuid", "{guid}");
    write_file(card / ".Trashes" / "501" / "IMG_0000.JPG", jpeg);

    const auto inventory = inventory_of(root);
    CHECK(inventory.outcome == ScanOutcome::completed);
    CHECK(media_paths(inventory)
          == std::vector<std::string>{"DCIM/100CANON/IMG_0001.CR3", "DCIM/100CANON/IMG_0001.JPG",
                                      "DCIM/100CANON/IMG_0003.JPG", "DCIM/100CANON/MVI_0002.MP4"});
    // Une paire RAW + JPEG donne deux photos.
    CHECK(item(inventory, "DCIM/100CANON/IMG_0001.CR3").format == MediaFormat::cr3);
    CHECK(item(inventory, "DCIM/100CANON/IMG_0001.CR3").signature.verdict == SignatureVerdict::matches);
    CHECK(companion_paths(item(inventory, "DCIM/100CANON/MVI_0002.MP4"))
          == std::vector<std::string>{"DCIM/100CANON/MVI_0002.THM"});
    CHECK(item(inventory, "DCIM/100CANON/MVI_0002.MP4").companions.front().kind == CompanionKind::thm);
    // Le fichier vide est signalé, pas écarté.
    CHECK(item(inventory, "DCIM/100CANON/IMG_0003.JPG").signature.verdict == SignatureVerdict::mismatch);
    CHECK(inventory.anomaly_count() == 1);

    CHECK(paths_of(inventory.excluded)
          == std::vector<std::string>{".Trashes/501/IMG_0000.JPG", "DCIM/100CANON/._IMG_0001.JPG",
                                      "System Volume Information/IndexerVolumeGuid"});
    CHECK(paths_of(inventory.ignored) == std::vector<std::string>{"MISC/AUTPRINT.MRK"});
    CHECK(inventory.orphan_companions.empty());
    CHECK(inventory.errors.empty());
}

TEST_CASE("inventaire d'un telephone, iPhone et Android")
{
    TempDir root;
    const auto apple = root.path() / "DCIM" / "100APPLE";
    write_file(apple / "IMG_1234.HEIC", heic);
    write_file(apple / "IMG_1234.MOV", mov);
    write_file(apple / "IMG_1234.AAE", xml);
    write_file(apple / "IMG_E1234.HEIC", heic);
    write_file(apple / "IMG_O1234.AAE", xml);
    write_file(apple / "IMG_1235.MOV", mov);
    const auto camera = root.path() / "DCIM" / "Camera";
    write_file(camera / "PXL_20241231_120000000.jpg", jpeg);
    write_file(camera / ".trashed-1735689600-PXL_20241230_090000000.jpg", jpeg);
    write_file(root.path() / "DCIM" / ".thumbnails" / "1700000000000.jpg", jpeg);
    write_file(root.path() / "Android" / "data" / "com.exemple" / "cache.bin", "x");

    const auto inventory = inventory_of(root);
    CHECK(inventory.outcome == ScanOutcome::completed);
    CHECK(media_paths(inventory)
          == std::vector<std::string>{"DCIM/100APPLE/IMG_1234.HEIC", "DCIM/100APPLE/IMG_1235.MOV",
                                      "DCIM/100APPLE/IMG_E1234.HEIC", "DCIM/Camera/PXL_20241231_120000000.jpg"});
    // La photo animée emmène sa vidéo et ses deux .aae ; la retouchée est à part.
    const auto& live = item(inventory, "DCIM/100APPLE/IMG_1234.HEIC");
    CHECK(companion_paths(live)
          == std::vector<std::string>{"DCIM/100APPLE/IMG_1234.AAE", "DCIM/100APPLE/IMG_1234.MOV",
                                      "DCIM/100APPLE/IMG_O1234.AAE"});
    CHECK(live.companions[1].kind == CompanionKind::live_video);
    CHECK(item(inventory, "DCIM/100APPLE/IMG_E1234.HEIC").companions.empty());
    CHECK(inventory.anomaly_count() == 0);

    CHECK(inventory.excluded.size() == 2);
    CHECK(paths_of(inventory.ignored) == std::vector<std::string>{"Android/data/com.exemple/cache.bin"});
    CHECK(inventory.orphan_companions.empty());
}

TEST_CASE("inventaire d'un dossier en desordre")
{
    TempDir root;
    const auto photos = root.path() / "Photos";
    write_file(photos / "vacances" / "photo.jpg", jpeg);
    write_file(photos / "vacances" / "photo.PNG", jpeg); // un JPEG mal nommé
    write_file(photos / utf8_path("Été 2026") / "plage.webp", webp);
    write_file(photos / "film.mkv", mkv);
    write_file(photos / "notes.txt", "à lire");
    write_file(photos / "sans_extension", jpeg);
    write_file(photos / "perdu.xmp", xml);
    write_file(photos / "DJI_0001.SRT", "1\n00:00:00,000 --> 00:00:00,033\n");
    write_file(photos / ".DS_Store", "x");

    const auto inventory = inventory_of(root);
    CHECK(inventory.outcome == ScanOutcome::completed);
    CHECK(media_paths(inventory)
          == std::vector<std::string>{"Photos/film.mkv", "Photos/vacances/photo.PNG", "Photos/vacances/photo.jpg",
                                      "Photos/\xC3\x89t\xC3\xA9 2026/plage.webp"});
    const auto& misnamed = item(inventory, "Photos/vacances/photo.PNG");
    CHECK(misnamed.signature.verdict == SignatureVerdict::mismatch);
    CHECK(misnamed.signature.detected == MediaFormat::jpeg);
    CHECK(inventory.anomaly_count() == 1);

    CHECK(paths_of(inventory.orphan_companions) == std::vector<std::string>{"Photos/DJI_0001.SRT", "Photos/perdu.xmp"});
    CHECK(paths_of(inventory.ignored) == std::vector<std::string>{"Photos/notes.txt", "Photos/sans_extension"});
    CHECK(paths_of(inventory.excluded) == std::vector<std::string>{"Photos/.DS_Store"});
}

TEST_CASE("chaque fichier se retrouve dans une seule liste")
{
    ScriptedSource source;
    source.add("a/IMG_1.HEIC", heic);
    source.add("a/IMG_1.MOV", mov);
    source.add("a/IMG_1.xmp", xml);
    source.add("a/IMG_2.JPG", jpeg, true); // attribut caché
    source.add("a/IMG_3.JPG", jpeg);
    source.add("a/IMG_9.xmp", xml);
    source.add("a/readme", "x");
    source.add("a/.hidden.jpg", jpeg);

    const auto inventory = take_inventory(source, {});
    std::vector<std::string> seen;
    for (const auto& media : inventory.media) {
        seen.push_back(media.entry.relative_path);
        for (const auto& companion : media.companions)
            seen.push_back(companion.entry.relative_path);
    }
    for (const auto* list : {&inventory.orphan_companions, &inventory.excluded, &inventory.ignored}) {
        for (const auto& entry : *list)
            seen.push_back(entry.relative_path);
    }
    std::ranges::sort(seen);
    auto expected = paths_of(source.entries);
    std::ranges::sort(expected);
    CHECK(seen == expected);
    CHECK(paths_of(inventory.excluded) == std::vector<std::string>{"a/.hidden.jpg", "a/IMG_2.JPG"});
}

TEST_CASE("la progression couvre le parcours puis les signatures")
{
    ScriptedSource source;
    source.add("IMG_1.JPG", jpeg);
    source.add("IMG_1.xmp", xml);
    source.add("IMG_2.JPG", jpeg);
    source.add("notes.txt", "x");

    std::vector<InventoryProgress> steps;
    const auto inventory = take_inventory(source, {}, [&](const InventoryProgress& p) { steps.push_back(p); });
    REQUIRE(steps.size() == 4 + 2);
    for (std::size_t i = 0; i < 4; ++i) {
        CHECK(steps[i].phase == InventoryProgress::Phase::scanning);
        CHECK(steps[i].done == i + 1);
    }
    CHECK(steps[4].phase == InventoryProgress::Phase::checking);
    CHECK(steps[4].done == 1);
    CHECK(steps[4].total == 2);
    CHECK(steps[5].done == 2);
    CHECK(steps[5].total == 2);
    CHECK(inventory.media.size() == 2);
}

TEST_CASE("un inventaire interrompu ne rend rien d'exploitable")
{
    ScriptedSource source;
    source.add("IMG_1.JPG", jpeg);
    source.add("IMG_2.JPG", jpeg);
    source.add("notes.txt", "x");

    SECTION("pendant le parcours")
    {
        std::stop_source stop;
        stop.request_stop();
        const auto inventory = take_inventory(source, stop.get_token());
        CHECK(inventory.outcome == ScanOutcome::interrupted);
        CHECK(inventory.media.empty());
        CHECK(inventory.ignored.empty());
    }
    SECTION("pendant la lecture des signatures")
    {
        std::stop_source stop;
        const auto inventory = take_inventory(source, stop.get_token(), [&](const InventoryProgress& p) {
            if (p.phase == InventoryProgress::Phase::checking)
                stop.request_stop();
        });
        CHECK(inventory.outcome == ScanOutcome::interrupted);
        CHECK(inventory.media.empty());
        CHECK(inventory.ignored.empty());
    }
}

TEST_CASE("un fichier illisible est une anomalie, une source perdue arrete tout")
{
    ScriptedSource source;
    source.add("IMG_1.JPG", jpeg);
    source.add("IMG_2.JPG", jpeg);

    SECTION("la source reste la")
    {
        source.unreadable.insert("IMG_1.JPG");
        const auto inventory = take_inventory(source, {});
        CHECK(inventory.outcome == ScanOutcome::completed);
        CHECK(item(inventory, "IMG_1.JPG").signature.verdict == SignatureVerdict::unreadable);
        CHECK(item(inventory, "IMG_2.JPG").signature.verdict == SignatureVerdict::matches);
        CHECK(inventory.anomaly_count() == 1);
    }
    SECTION("la source disparait")
    {
        source.vanishes_on_read = true;
        const auto inventory = take_inventory(source, {});
        CHECK(inventory.outcome == ScanOutcome::source_unavailable);
        CHECK(inventory.media.empty());
    }
}

TEST_CASE("une source absente donne un inventaire vide")
{
    TempDir root;
    DirectorySource source(root.path() / "absente");
    const auto inventory = take_inventory(source, {});
    CHECK(inventory.outcome == ScanOutcome::source_unavailable);
    CHECK(inventory.media.empty());
    CHECK(inventory.anomaly_count() == inventory.errors.size());
}
