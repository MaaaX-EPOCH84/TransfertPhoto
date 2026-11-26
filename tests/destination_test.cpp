#include "core/destination.hpp"

#include "core/directory_source.hpp"
#include "test_files.hpp"

#include <catch2/catch_test_macros.hpp>

#include <stop_token>
#include <string>
#include <vector>

// Noms de tests sans accents : voir version_test.cpp.

using test_support::TempDir;
using test_support::write_file;
using transfertphoto::DirectorySource;
using transfertphoto::Inventory;
using transfertphoto::InventoryProgress;
using transfertphoto::ScanOutcome;
using transfertphoto::take_destination_inventory;

namespace {

const std::string jpeg_head = std::string("\xFF\xD8\xFF\xE0", 4) + "JFIF";

std::vector<std::string> media_paths(const Inventory& inventory)
{
    std::vector<std::string> paths;
    for (const auto& item : inventory.media)
        paths.push_back(item.entry.relative_path);
    return paths;
}

} // namespace

TEST_CASE("une destination rangee par date est inventoriee en entier")
{
    TempDir root;
    write_file(root.path() / "2023" / "12" / "25" / "IMG_0001.JPG", jpeg_head);
    write_file(root.path() / "2023" / "12" / "25" / "IMG_0001.xmp", "<x/>");
    write_file(root.path() / "2024" / "01" / "02" / "IMG_0002.JPG", jpeg_head);
    write_file(root.path() / "__A_TRIER__" / "2000" / "01" / "01" / "DSC_0001.JPG", jpeg_head);
    write_file(root.path() / "2024" / "notes.txt", "liste");
    write_file(root.path() / ".DS_Store", "x");

    DirectorySource destination(root.path());
    const auto inventory = take_destination_inventory(destination, {});
    CHECK(inventory.outcome == ScanOutcome::completed);
    CHECK(media_paths(inventory)
          == std::vector<std::string>{"2023/12/25/IMG_0001.JPG", "2024/01/02/IMG_0002.JPG",
                                      "__A_TRIER__/2000/01/01/DSC_0001.JPG"});
    REQUIRE(inventory.media[0].companions.size() == 1);
    CHECK(inventory.media[0].companions[0].entry.relative_path == "2023/12/25/IMG_0001.xmp");
    CHECK(inventory.ignored.size() == 1);
    CHECK(inventory.excluded.size() == 1);
}

TEST_CASE("une destination a plat est inventoriee")
{
    TempDir root;
    write_file(root.path() / "IMG_0001.JPG", jpeg_head);
    write_file(root.path() / "IMG_0002.JPG", jpeg_head);

    DirectorySource destination(root.path());
    const auto inventory = take_destination_inventory(destination, {});
    CHECK(media_paths(inventory) == std::vector<std::string>{"IMG_0001.JPG", "IMG_0002.JPG"});
}

TEST_CASE("une destination absente est un dossier vide")
{
    TempDir root;
    DirectorySource destination(root.path() / "Photos" / "pas encore creee");
    const auto inventory = take_destination_inventory(destination, {});
    CHECK(inventory.outcome == ScanOutcome::completed);
    CHECK(inventory.media.empty());
    CHECK(inventory.errors.empty());
}

TEST_CASE("l'inventaire de la destination remonte sa progression et s'interrompt")
{
    TempDir root;
    for (int i = 0; i < 4; ++i)
        write_file(root.path() / ("IMG_000" + std::to_string(i) + ".JPG"), jpeg_head);
    DirectorySource destination(root.path());

    std::vector<InventoryProgress> seen;
    (void)take_destination_inventory(destination, {}, [&](const InventoryProgress& p) { seen.push_back(p); });
    CHECK(seen.size() == 8); // quatre fichiers vus, quatre signatures

    std::stop_source stop;
    stop.request_stop();
    const auto interrupted = take_destination_inventory(destination, stop.get_token());
    CHECK(interrupted.outcome == ScanOutcome::interrupted);
    CHECK(interrupted.media.empty());
}
