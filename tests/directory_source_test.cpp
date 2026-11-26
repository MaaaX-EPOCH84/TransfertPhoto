#include "core/directory_source.hpp"

#include "test_files.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// Noms de tests sans accents : voir version_test.cpp.

namespace fs = std::filesystem;
using transfertphoto::DirectorySource;
using transfertphoto::ScanOutcome;
using transfertphoto::ScanResult;
using transfertphoto::SourceEntry;

using test_support::TempDir;
using test_support::utf8_path;
using test_support::write_file;

namespace {

struct Collected {
    ScanResult result;
    std::vector<SourceEntry> entries;
};

Collected collect(DirectorySource& source)
{
    Collected collected;
    collected.result = source.scan(
        [&](const SourceEntry& entry) { collected.entries.push_back(entry); }, {});
    std::sort(collected.entries.begin(), collected.entries.end(),
              [](const SourceEntry& a, const SourceEntry& b) { return a.relative_path < b.relative_path; });
    return collected;
}

std::vector<std::string> paths_of(const Collected& collected)
{
    std::vector<std::string> paths;
    for (const auto& entry : collected.entries) {
        paths.push_back(entry.relative_path);
    }
    return paths;
}

} // namespace

TEST_CASE("un dossier est parcouru en entier, sous-dossiers compris")
{
    TempDir root;
    write_file(root.path() / "a.jpg", "a");
    write_file(root.path() / "DCIM" / "100CANON" / "IMG_0001.CR3", "bb");
    write_file(root.path() / "DCIM" / "100CANON" / "IMG_0002.JPG", "ccc");
    fs::create_directories(root.path() / "vide");

    DirectorySource source(root.path());
    const auto collected = collect(source);

    CHECK(collected.result.outcome == ScanOutcome::completed);
    CHECK(collected.result.errors.empty());
    CHECK(paths_of(collected)
          == std::vector<std::string>{"DCIM/100CANON/IMG_0001.CR3", "DCIM/100CANON/IMG_0002.JPG", "a.jpg"});
}

TEST_CASE("les chemins sont relatifs, en UTF-8, avec des barres obliques")
{
    TempDir root;
    write_file(root.path() / utf8_path("Été 2026") / utf8_path("plage à Noël.jpg"), "x");

    DirectorySource source(root.path());
    const auto collected = collect(source);

    REQUIRE(collected.entries.size() == 1);
    CHECK(collected.entries[0].relative_path == "Été 2026/plage à Noël.jpg");
}

TEST_CASE("une racine avec une barre oblique finale")
{
    TempDir root;
    write_file(root.path() / "sous" / "photo.jpg", "x");

    DirectorySource source(root.path() / "");
    const auto collected = collect(source);

    CHECK(paths_of(collected) == std::vector<std::string>{"sous/photo.jpg"});
}

TEST_CASE("la taille et la date de chaque fichier sont lues")
{
    using namespace std::chrono_literals;
    TempDir root;
    const fs::path file = root.path() / "photo.jpg";
    write_file(file, "0123456789");
    fs::last_write_time(file, fs::file_time_type::clock::now() - 48h);

    DirectorySource source(root.path());
    const auto collected = collect(source);

    REQUIRE(collected.entries.size() == 1);
    CHECK(collected.entries[0].size == 10);
    const auto expected = std::chrono::system_clock::now() - 48h;
    const auto gap = collected.entries[0].modified - expected;
    CHECK(gap < 1min);
    CHECK(gap > -1min);
}

TEST_CASE("read_head lit les premiers octets")
{
    TempDir root;
    write_file(root.path() / "sous" / "court.jpg", "\xFF\xD8\xFF");
    write_file(root.path() / "long.png", "\x89PNG\r\n\x1A\n-suite");

    DirectorySource source(root.path());
    std::array<std::byte, 8> buffer{};
    std::error_code error;

    SECTION("fichier plus long que le tampon")
    {
        CHECK(source.read_head("long.png", buffer, error) == 8);
        CHECK(!error);
        CHECK(buffer[0] == std::byte{0x89});
        CHECK(buffer[7] == std::byte{0x0A});
    }
    SECTION("fichier plus court que le tampon, dans un sous-dossier")
    {
        CHECK(source.read_head("sous/court.jpg", buffer, error) == 3);
        CHECK(!error);
        CHECK(buffer[0] == std::byte{0xFF});
        CHECK(buffer[2] == std::byte{0xFF});
    }
    SECTION("fichier absent")
    {
        CHECK(source.read_head("absent.jpg", buffer, error) == 0);
        CHECK(error == std::errc::no_such_file_or_directory);
    }
}

TEST_CASE("read_head accepte un chemin UTF-8")
{
    TempDir root;
    write_file(root.path() / utf8_path("Été") / utf8_path("Noël.jpg"), "xyz");

    DirectorySource source(root.path());
    std::array<std::byte, 8> buffer{};
    std::error_code error;
    CHECK(source.read_head("Été/Noël.jpg", buffer, error) == 3);
    CHECK(!error);
}

TEST_CASE("read_range lit n'importe quelle plage du fichier")
{
    TempDir root;
    write_file(root.path() / "photo.jpg", "0123456789");

    DirectorySource source(root.path());
    std::array<std::byte, 4> buffer{};
    std::error_code error;

    auto text = [&](std::size_t count) {
        std::string result;
        for (std::size_t i = 0; i < count; ++i)
            result += static_cast<char>(buffer[i]);
        return result;
    };

    SECTION("en tete")
    {
        CHECK(source.read_range("photo.jpg", 0, buffer, error) == 4);
        CHECK(!error);
        CHECK(text(4) == "0123");
    }
    SECTION("au milieu")
    {
        CHECK(source.read_range("photo.jpg", 3, buffer, error) == 4);
        CHECK(!error);
        CHECK(text(4) == "3456");
    }
    SECTION("en fin")
    {
        CHECK(source.read_range("photo.jpg", 6, buffer, error) == 4);
        CHECK(!error);
        CHECK(text(4) == "6789");
    }
    SECTION("a cheval sur la fin, lecture tronquee")
    {
        CHECK(source.read_range("photo.jpg", 8, buffer, error) == 2);
        CHECK(!error);
        CHECK(text(2) == "89");
    }
    SECTION("a partir de la fin et au-dela, rien sans erreur")
    {
        CHECK(source.read_range("photo.jpg", 10, buffer, error) == 0);
        CHECK(!error);
        CHECK(source.read_range("photo.jpg", 1000, buffer, error) == 0);
        CHECK(!error);
    }
    SECTION("tampon de taille nulle")
    {
        CHECK(source.read_range("photo.jpg", 2, std::span<std::byte>{}, error) == 0);
        CHECK(!error);
    }
    SECTION("fichier disparu")
    {
        fs::remove(root.path() / "photo.jpg");
        CHECK(source.read_range("photo.jpg", 2, buffer, error) == 0);
        CHECK(error == std::errc::no_such_file_or_directory);
    }
}

TEST_CASE("un dossier donne le chemin local de ses fichiers")
{
    TempDir root;
    DirectorySource source(root.path());
    const auto path = source.local_path("Été/Noël.jpg");
    REQUIRE(path);
    CHECK(*path == root.path() / utf8_path("Été") / utf8_path("Noël.jpg"));
}

TEST_CASE("un lien symbolique vers un dossier n'est pas suivi")
{
    TempDir root;
    TempDir elsewhere;
    write_file(elsewhere.path() / "ailleurs.jpg", "x");
    write_file(root.path() / "ici.jpg", "x");

    std::error_code error;
    fs::create_directory_symlink(elsewhere.path(), root.path() / "lien-dossier", error);
    if (error) {
        SKIP("liens symboliques indisponibles ici : " << error.message());
    }
    fs::create_symlink(elsewhere.path() / "ailleurs.jpg", root.path() / "lien-fichier.jpg", error);
    REQUIRE(!error);

    DirectorySource source(root.path());
    const auto collected = collect(source);

    CHECK(collected.result.outcome == ScanOutcome::completed);
    CHECK(paths_of(collected) == std::vector<std::string>{"ici.jpg", "lien-fichier.jpg"});
}

TEST_CASE("un dossier illisible n'interrompt pas le parcours")
{
    TempDir root;
    write_file(root.path() / "ferme" / "cache.jpg", "x");
    write_file(root.path() / "ouvert" / "vu.jpg", "x");

    fs::permissions(root.path() / "ferme", fs::perms::none);
    std::error_code probe;
    fs::directory_iterator check(root.path() / "ferme", probe);
    if (!probe) {
        fs::permissions(root.path() / "ferme", fs::perms::owner_all);
        SKIP("dossier lisible malgre les droits retires (compte administrateur, ou Windows)");
    }

    DirectorySource source(root.path());
    const auto collected = collect(source);
    fs::permissions(root.path() / "ferme", fs::perms::owner_all);

    CHECK(collected.result.outcome == ScanOutcome::completed);
    CHECK(paths_of(collected) == std::vector<std::string>{"ouvert/vu.jpg"});
    REQUIRE(collected.result.errors.size() == 1);
    CHECK(collected.result.errors[0].relative_path == "ferme");
    CHECK(collected.result.errors[0].error);
}

TEST_CASE("le stop_token interrompt le parcours")
{
    TempDir root;
    write_file(root.path() / "1.jpg", "x");
    write_file(root.path() / "2.jpg", "x");
    write_file(root.path() / "3.jpg", "x");

    DirectorySource source(root.path());
    std::stop_source stop;
    int seen = 0;
    const auto result = source.scan(
        [&](const SourceEntry&) {
            ++seen;
            stop.request_stop();
        },
        stop.get_token());

    CHECK(result.outcome == ScanOutcome::interrupted);
    CHECK(seen == 1);
}

TEST_CASE("une source absente n'est pas disponible")
{
    TempDir root;
    DirectorySource source(root.path() / "carte-retiree");

    CHECK(!source.is_available());
    int seen = 0;
    const auto result = source.scan([&](const SourceEntry&) { ++seen; }, {});
    CHECK(result.outcome == ScanOutcome::source_unavailable);
    CHECK(seen == 0);
}

TEST_CASE("la source perdue en plein parcours rend source_unavailable")
{
    TempDir parent;
    const fs::path card = parent.path() / "carte";
    write_file(card / "racine.jpg", "x");
    write_file(card / "DCIM" / "a.jpg", "x");
    write_file(card / "MISC" / "b.jpg", "x");

    DirectorySource source(card);
    std::error_code removal;
    bool removed = false;
    const auto result = source.scan(
        [&](const SourceEntry&) {
            if (!removed) {
                removed = true;
                fs::remove_all(card, removal);
            }
        },
        {});

    if (removal || fs::exists(card)) {
        SKIP("impossible de retirer un dossier en cours de parcours ici");
    }
    CHECK(result.outcome == ScanOutcome::source_unavailable);
    CHECK(result.errors.empty());
}

#ifdef _WIN32
TEST_CASE("attribut hidden de Windows, y compris par un dossier parent")
{
    TempDir root;
    write_file(root.path() / "visible.jpg", "x");
    write_file(root.path() / "masque.jpg", "x");
    write_file(root.path() / "dossier-masque" / "dedans.jpg", "x");
    REQUIRE(::SetFileAttributesW((root.path() / "masque.jpg").c_str(), FILE_ATTRIBUTE_HIDDEN));
    REQUIRE(::SetFileAttributesW((root.path() / "dossier-masque").c_str(), FILE_ATTRIBUTE_HIDDEN));

    DirectorySource source(root.path());
    const auto collected = collect(source);

    REQUIRE(collected.entries.size() == 3);
    CHECK(collected.entries[0].relative_path == "dossier-masque/dedans.jpg");
    CHECK(collected.entries[0].hidden);
    CHECK(collected.entries[1].relative_path == "masque.jpg");
    CHECK(collected.entries[1].hidden);
    CHECK(collected.entries[2].relative_path == "visible.jpg");
    CHECK(!collected.entries[2].hidden);
}
#else
TEST_CASE("hors Windows, un nom en point ne rend pas un fichier hidden")
{
    TempDir root;
    write_file(root.path() / ".thumbnails" / "t.jpg", "x");
    write_file(root.path() / ".trashed-1700000000-IMG.jpg", "x");

    DirectorySource source(root.path());
    const auto collected = collect(source);

    REQUIRE(collected.entries.size() == 2);
    for (const auto& entry : collected.entries) {
        CHECK(!entry.hidden);
    }
}
#endif
