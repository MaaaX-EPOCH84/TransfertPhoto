#include "core/trash.hpp"

#include "test_files.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>

// Noms de tests sans accents : voir version_test.cpp.

namespace fs = std::filesystem;
using namespace transfertphoto;
using test_support::TempDir;
using test_support::utf8_path;
using test_support::write_file;

namespace {

std::string read_text(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::string to_utf8(const fs::path& path)
{
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

} // namespace

TEST_CASE("les chemins des fiches de la corbeille s'encodent comme une URL")
{
    CHECK(trash_detail::encode_trash_path("/home/max/Photos/IMG_0001.JPG") == "/home/max/Photos/IMG_0001.JPG");
    CHECK(trash_detail::encode_trash_path("/home/a b/Été.jpg") == "/home/a%20b/%C3%89t%C3%A9.jpg");
    CHECK(trash_detail::encode_trash_path("x%y#z") == "x%25y%23z");
}

TEST_CASE("les systemes de fichiers en reseau sont reconnus")
{
    for (const auto type : {"nfs", "nfs4", "cifs", "smb3", "fuse.sshfs", "fuse.gvfsd-fuse"}) {
        INFO(type);
        CHECK(trash_detail::is_network_filesystem(type));
    }
    for (const auto type : {"ext4", "btrfs", "xfs", "vfat", "exfat", "tmpfs", "ntfs3"}) {
        INFO(type);
        CHECK_FALSE(trash_detail::is_network_filesystem(type));
    }
}

TEST_CASE("une suppression definitive efface le fichier")
{
    TempDir root;
    write_file(root.path() / "a.jpg", "x");
    CHECK_FALSE(delete_permanently(root.path() / "a.jpg"));
    CHECK_FALSE(fs::exists(root.path() / "a.jpg"));
    CHECK(delete_permanently(root.path() / "a.jpg"));
}

#if defined(__linux__)

namespace {

// Une corbeille d'utilisateur à part, sur le même disque que les fichiers.
class IsolatedTrash {
public:
    IsolatedTrash()
    {
        if (const char* saved = std::getenv("XDG_DATA_HOME"))
            saved_ = saved;
        setenv("XDG_DATA_HOME", (root_.path() / "data").c_str(), 1);
    }
    ~IsolatedTrash()
    {
        if (saved_)
            setenv("XDG_DATA_HOME", saved_->c_str(), 1);
        else
            unsetenv("XDG_DATA_HOME");
    }
    IsolatedTrash(const IsolatedTrash&) = delete;
    IsolatedTrash& operator=(const IsolatedTrash&) = delete;

    [[nodiscard]] fs::path trash() const { return root_.path() / "data" / "Trash"; }

private:
    TempDir root_;
    std::optional<std::string> saved_;
};

} // namespace

TEST_CASE("la corbeille de freedesktop.org recoit le fichier et sa fiche")
{
    IsolatedTrash isolated;
    TempDir root;
    const auto file = root.path() / utf8_path("Été") / utf8_path("Noël.jpg");
    write_file(file, "photo de Noël");
    REQUIRE(volume_has_trash(file));

    const auto result = move_to_trash(file);
    REQUIRE(result.outcome == TrashOutcome::trashed);
    CHECK_FALSE(fs::exists(file));
    CHECK(read_text(isolated.trash() / "files" / utf8_path("Noël.jpg")) == "photo de Noël");

    const auto info = read_text(isolated.trash() / "info" / utf8_path("Noël.jpg.trashinfo"));
    CHECK(info.starts_with("[Trash Info]\nPath=" + trash_detail::encode_trash_path(to_utf8(file)) + "\n"));
    const auto date = info.find("DeletionDate=");
    REQUIRE(date != std::string::npos);
    // AAAA-MM-JJTHH:MM:SS
    CHECK(info.size() >= date + 13 + 19);
    CHECK(info[date + 13 + 10] == 'T');
}

TEST_CASE("deux fichiers du meme nom ne s'ecrasent pas dans la corbeille")
{
    IsolatedTrash isolated;
    TempDir root;
    write_file(root.path() / "a" / "IMG_0001.JPG", "premier");
    write_file(root.path() / "b" / "IMG_0001.JPG", "second");
    REQUIRE(move_to_trash(root.path() / "a" / "IMG_0001.JPG").outcome == TrashOutcome::trashed);
    REQUIRE(move_to_trash(root.path() / "b" / "IMG_0001.JPG").outcome == TrashOutcome::trashed);
    CHECK(read_text(isolated.trash() / "files" / "IMG_0001.JPG") == "premier");
    CHECK(read_text(isolated.trash() / "files" / "IMG_0001 (2).JPG") == "second");
    CHECK(fs::exists(isolated.trash() / "info" / "IMG_0001 (2).JPG.trashinfo"));
}

TEST_CASE("un fichier absent ne va pas a la corbeille et ne laisse pas de fiche")
{
    IsolatedTrash isolated;
    TempDir root;
    const auto result = move_to_trash(root.path() / "absent.jpg");
    CHECK(result.outcome == TrashOutcome::failed);
    CHECK(result.error);
    std::error_code error;
    CHECK((!fs::exists(isolated.trash() / "info") || fs::is_empty(isolated.trash() / "info", error)));
}

#else

TEST_CASE("la corbeille du systeme recoit un fichier")
{
    TempDir root;
    const auto file = root.path() / utf8_path("Noël.jpg");
    write_file(file, "photo de Noël");
    const auto result = move_to_trash(file);
    // Le dossier temporaire de la machine de CI a une corbeille ; sinon le
    // fichier reste et c'est dit.
    if (volume_has_trash(file)) {
        CHECK(result.outcome == TrashOutcome::trashed);
        CHECK_FALSE(fs::exists(file));
    } else {
        CHECK(result.outcome == TrashOutcome::unavailable);
        CHECK(fs::exists(file));
    }
}

#endif
