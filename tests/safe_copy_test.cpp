#include "core/safe_copy.hpp"

#include "core/directory_source.hpp"
#include "memory_source.hpp"
#include "test_files.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <stop_token>
#include <string>
#include <vector>

// Noms de tests sans accents : voir version_test.cpp.

namespace fs = std::filesystem;
using namespace transfertphoto;
using test_support::MemorySource;
using test_support::TempDir;
using test_support::utf8_path;
using test_support::write_file;

namespace {

using Bytes = std::vector<std::uint8_t>;

Bytes content(std::size_t size)
{
    Bytes bytes(size);
    for (std::size_t i = 0; i < size; ++i)
        bytes[i] = static_cast<std::uint8_t>((i * 7) % 251);
    return bytes;
}

Bytes read_back(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

constexpr std::size_t big = 3 * 1024 * 1024 + 17;
const auto modified = std::chrono::system_clock::time_point(std::chrono::seconds(1'620'284'889));

// Une source en mémoire et un dossier de destination.
struct Fixture {
    MemorySource source;
    TempDir root;

    explicit Fixture(std::size_t size = big) { source.contents["DCIM/IMG_0001.MP4"] = content(size); }

    CopyResult copy(const std::string& name = "IMG_0001.MP4", bool verify = false, std::stop_token stop = {},
                    const std::function<void(std::uint64_t)>& on_bytes = {})
    {
        const auto size = source.contents["DCIM/IMG_0001.MP4"].size();
        return copy_safely({source, "DCIM/IMG_0001.MP4", size, modified, root.path() / "2021" / "05" / "06", name,
                            verify},
                           stop, on_bytes);
    }

    fs::path folder() const { return root.path() / "2021" / "05" / "06"; }

    // Tout ce qui se trouve dans le dossier cible, fichiers cachés compris.
    std::vector<std::string> listing() const
    {
        std::vector<std::string> names;
        std::error_code error;
        for (const auto& entry : fs::directory_iterator(folder(), error)) {
            const auto name = entry.path().filename().u8string();
            names.emplace_back(name.begin(), name.end());
        }
        std::ranges::sort(names);
        return names;
    }
};

} // namespace

TEST_CASE("une copie sure arrive entiere, sous son nom, avec sa date")
{
    Fixture f;
    const auto result = f.copy();
    REQUIRE(result.outcome == CopyResult::Outcome::copied);
    CHECK(result.path == f.folder() / "IMG_0001.MP4");
    CHECK(read_back(result.path) == f.source.contents["DCIM/IMG_0001.MP4"]);
    CHECK(result.hash == hash_bytes(std::as_bytes(std::span(f.source.contents["DCIM/IMG_0001.MP4"]))));
    CHECK_FALSE(result.verified);
    CHECK(f.listing() == std::vector<std::string>{"IMG_0001.MP4"});

    const auto written = fs::last_write_time(result.path);
    const auto gap = std::chrono::duration_cast<std::chrono::seconds>(
        written - fs::file_time_type::clock::now() - (modified - std::chrono::system_clock::now()));
    CHECK(gap.count() >= -1);
    CHECK(gap.count() <= 1);
}

TEST_CASE("la copie relue est verifiee")
{
    Fixture f;
    std::uint64_t bytes = 0;
    const auto result = f.copy("IMG_0001.MP4", true, {}, [&](std::uint64_t n) { bytes += n; });
    REQUIRE(result.outcome == CopyResult::Outcome::copied);
    CHECK(result.verified);
    CHECK(bytes == 2 * big);
}

TEST_CASE("une copie qui ne concorde pas se repere")
{
    Fixture f(1000);
    write_file(f.root.path() / "copie.jpg", "pas la bonne copie");
    std::error_code error;
    CHECK_FALSE(matches_hash(f.root.path() / "copie.jpg", 18, hash_bytes({}), {}, error));
    const std::string text = "pas la bonne copie";
    CHECK(matches_hash(f.root.path() / "copie.jpg", 18, hash_bytes(std::as_bytes(std::span(text))), {}, error));
}

TEST_CASE("un nom deja pris n'est jamais ecrase")
{
    Fixture f(1000);
    write_file(f.folder() / "IMG_0001.MP4", "autre photo");
    const auto result = f.copy();
    CHECK(result.outcome == CopyResult::Outcome::name_taken);
    std::ifstream kept(f.folder() / "IMG_0001.MP4");
    CHECK(std::string(std::istreambuf_iterator<char>(kept), {}) == "autre photo");
    CHECK(f.listing() == std::vector<std::string>{"IMG_0001.MP4"});
}

TEST_CASE("un nom pris au dernier moment n'est pas ecrase non plus")
{
    TempDir root;
    write_file(root.path() / ".a.transfertphoto", "nouveau");
    write_file(root.path() / "a.jpg", "ancien");
    std::error_code error;
    CHECK(rename_without_replacing(root.path() / ".a.transfertphoto", root.path() / "a.jpg", error)
          == RenameOutcome::target_exists);
    std::ifstream kept(root.path() / "a.jpg");
    CHECK(std::string(std::istreambuf_iterator<char>(kept), {}) == "ancien");
    CHECK(fs::exists(root.path() / ".a.transfertphoto"));

    CHECK(rename_without_replacing(root.path() / ".a.transfertphoto", root.path() / "b.jpg", error)
          == RenameOutcome::renamed);
    CHECK(fs::exists(root.path() / "b.jpg"));
    CHECK_FALSE(fs::exists(root.path() / ".a.transfertphoto"));
}

TEST_CASE("un arret en cours de copie ne laisse rien")
{
    Fixture f;
    std::stop_source stop;
    const auto result = f.copy("IMG_0001.MP4", false, stop.get_token(), [&](std::uint64_t) { stop.request_stop(); });
    CHECK(result.outcome == CopyResult::Outcome::interrupted);
    CHECK(f.listing().empty());
}

TEST_CASE("une source illisible ou changee ne laisse rien")
{
    SECTION("illisible")
    {
        Fixture f(1000);
        f.source.failing.insert("DCIM/IMG_0001.MP4");
        const auto result = f.copy();
        CHECK(result.outcome == CopyResult::Outcome::read_failed);
        CHECK(result.error == std::errc::io_error);
        CHECK(f.listing().empty());
    }
    SECTION("plus courte que relevee")
    {
        Fixture f(1000);
        const auto result = copy_safely(
            {f.source, "DCIM/IMG_0001.MP4", 5000, modified, f.folder(), "IMG_0001.MP4", false}, {});
        CHECK(result.outcome == CopyResult::Outcome::changed);
        CHECK(f.listing().empty());
    }
    SECTION("plus longue que relevee")
    {
        Fixture f(1000);
        const auto result = copy_safely(
            {f.source, "DCIM/IMG_0001.MP4", 900, modified, f.folder(), "IMG_0001.MP4", false}, {});
        CHECK(result.outcome == CopyResult::Outcome::changed);
        CHECK(f.listing().empty());
    }
}

TEST_CASE("une copie va vers un chemin accentue")
{
    TempDir card;
    TempDir library;
    write_file(card.path() / utf8_path("Été") / utf8_path("Noël.jpg"), "photo de Noël");
    DirectorySource source(card.path());
    const auto result = copy_safely({source, "Été/Noël.jpg", 14, modified, library.path() / utf8_path("Hiver à Lyon"),
                                     "Noël (2).jpg", true},
                                    {});
    REQUIRE(result.outcome == CopyResult::Outcome::copied);
    CHECK(result.verified);
    CHECK(fs::exists(library.path() / utf8_path("Hiver à Lyon") / utf8_path("Noël (2).jpg")));
}

TEST_CASE("un fichier vide se copie")
{
    Fixture f(0);
    const auto result = f.copy();
    REQUIRE(result.outcome == CopyResult::Outcome::copied);
    CHECK(fs::file_size(result.path) == 0);
}

TEST_CASE("les fichiers temporaires d'un transfert interrompu s'effacent au suivant")
{
    TempDir root;
    write_file(root.path() / "2023" / "12" / "25" / ".IMG_0001.JPG.transfertphoto", "à moitié");
    write_file(root.path() / "2023" / ".IMG_0002.MP4.transfertphoto", "à moitié");
    write_file(root.path() / "2023" / "12" / "25" / "IMG_0003.JPG", "photo");
    write_file(root.path() / "2023" / ".transfertphoto", "pas le nôtre");
    write_file(root.path() / "2023" / "IMG.transfertphoto", "pas caché");

    CHECK(remove_leftover_temporaries(root.path()) == 2);
    CHECK_FALSE(fs::exists(root.path() / "2023" / "12" / "25" / ".IMG_0001.JPG.transfertphoto"));
    CHECK(fs::exists(root.path() / "2023" / "12" / "25" / "IMG_0003.JPG"));
    CHECK(fs::exists(root.path() / "2023" / ".transfertphoto"));
    CHECK(fs::exists(root.path() / "2023" / "IMG.transfertphoto"));
    CHECK(temporary_name("IMG_0001.JPG") == ".IMG_0001.JPG.transfertphoto");
}
