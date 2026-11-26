// Recherche de doublons de bout en bout, sur des arborescences fabriquées sur
// disque : inventaire de la source et de la destination, datation, groupes,
// situations, réponses, plan, et comparaison octet par octet avant chaque
// suppression.

#include "core/byte_compare.hpp"
#include "core/dating.hpp"
#include "core/destination.hpp"
#include "core/directory_source.hpp"
#include "core/duplicate_decisions.hpp"
#include "core/duplicate_situations.hpp"
#include "core/duplicates.hpp"
#include "core/inventory.hpp"
#include "exif_samples.hpp"
#include "test_files.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

// Noms de tests sans accents : voir version_test.cpp.

using namespace transfertphoto;
using test_support::TempDir;
using test_support::write_file;

namespace {

// Une photo datée, grossie d'un remplissage après sa fin pour dépasser les
// 64 Kio examinés d'abord. Deux dates de même longueur donnent deux photos
// de même taille au contenu différent.
std::string photo(const std::string& date, std::size_t filler = 100 * 1024)
{
    const auto jpeg = test_support::jpeg_with_capture_date(date);
    std::string bytes(jpeg.begin(), jpeg.end());
    bytes.append(filler, '\x5A');
    return bytes;
}

// Tout ce que la fenêtre aura sous la main après l'analyse.
struct Analysis {
    DirectorySource source;
    DirectorySource destination;
    Inventory source_inventory;
    Inventory destination_inventory;
    Dating destination_dating;
    DuplicateSearch search;

    Analysis(const std::filesystem::path& source_root, const std::filesystem::path& destination_root)
        : source(source_root), destination(destination_root)
    {
        source_inventory = take_inventory(source, {});
        destination_inventory = take_destination_inventory(destination, {});
        destination_dating = date_media(destination, destination_inventory, {});
        search = find_duplicates(source, source_inventory, destination, destination_inventory, {});
    }

    std::string path_of(const FileRef& ref) const
    {
        const auto& inventory = ref.side == Side::source ? source_inventory : destination_inventory;
        return (ref.side == Side::source ? "source:" : "destination:") + inventory.media[ref.index].entry.relative_path;
    }

    std::vector<std::vector<std::string>> groups() const
    {
        std::vector<std::vector<std::string>> result;
        for (const auto& group : search.groups) {
            std::vector<std::string> members;
            for (const auto& member : group.members)
                members.push_back(path_of(member));
            result.push_back(members);
        }
        return result;
    }

    DestinationDate date_of() const
    {
        return [this](std::size_t index) -> std::optional<LocalDateTime> {
            return destination_dating.media[index].retained().value;
        };
    }

    // Comparaison octet par octet d'une suppression prévue avec sa référence.
    Comparison verify(const MediaDeletion& deletion)
    {
        const auto file = [&](const FileRef& ref) -> FileToCompare {
            auto& side_source = ref.side == Side::source ? source : destination;
            const auto& inventory = ref.side == Side::source ? source_inventory : destination_inventory;
            const auto& entry = inventory.media[ref.index].entry;
            return {side_source, entry.relative_path, entry.size};
        };
        return compare_files(file(deletion.doomed), file(deletion.reference), {}).outcome;
    }
};

} // namespace

TEST_CASE("carte avec ses propres doublons, destination encore absente")
{
    TempDir card;
    TempDir library;
    write_file(card.path() / "DCIM" / "100CANON" / "IMG_0001.JPG", photo("2023:12:25 18:04:00"));
    write_file(card.path() / "DCIM" / "101CANON" / "IMG_0001.JPG", photo("2023:12:25 18:04:00"));
    write_file(card.path() / "DCIM" / "101CANON" / "IMG_0002.JPG", photo("2023:12:25 18:05:00"));
    write_file(card.path() / "DCIM" / "101CANON" / "IMG_0003.JPG", photo("2023:12:25 18:06:00", 10));

    Analysis analysis(card.path(), library.path() / "Photos");
    REQUIRE(analysis.search.outcome == ScanOutcome::completed);
    CHECK(analysis.destination_inventory.outcome == ScanOutcome::completed);
    CHECK(analysis.destination_inventory.media.empty());
    CHECK(analysis.groups()
          == std::vector<std::vector<std::string>>{
              {"source:DCIM/100CANON/IMG_0001.JPG", "source:DCIM/101CANON/IMG_0001.JPG"}});
    CHECK(situations_of(analysis.search.groups[0]).questions() == std::vector{DuplicateSituation::within_source});

    DuplicateAnswers answers;
    answers.all_within_source = WithinSourceChoice::transfer_one_delete_others;
    const auto plan = plan_duplicates(analysis.search, analysis.source_inventory, analysis.destination_inventory,
                                      answers, TransferMode::move, analysis.date_of());
    REQUIRE(plan);
    REQUIRE(plan->deletions.size() == 1);
    CHECK(analysis.path_of(plan->deletions[0].doomed) == "source:DCIM/101CANON/IMG_0001.JPG");
    CHECK(analysis.verify(plan->deletions[0]) == Comparison::identical);
}

TEST_CASE("bibliotheque rangee avec ses propres doublons")
{
    TempDir card;
    TempDir library;
    write_file(library.path() / "2023" / "12" / "25" / "IMG_0001.JPG", photo("2023:12:25 18:04:00"));
    write_file(library.path() / "2023" / "12" / "25" / "IMG_0001.xmp", "<x:xmpmeta/>");
    write_file(library.path() / "Divers" / "IMG_0001 (2).JPG", photo("2023:12:25 18:04:00"));
    write_file(library.path() / "Divers" / "IMG_0001 (2).xmp", "<x:xmpmeta/>");
    write_file(library.path() / "2024" / "01" / "02" / "IMG_0005.JPG", photo("2024:01:02 09:00:00"));

    Analysis analysis(card.path(), library.path());
    CHECK(analysis.groups()
          == std::vector<std::vector<std::string>>{
              {"destination:2023/12/25/IMG_0001.JPG", "destination:Divers/IMG_0001 (2).JPG"}});

    // La copie gardée est celle du dossier de sa date.
    DuplicateAnswers answers;
    answers.all_within_destination = WithinDestinationChoice::keep_one;
    const auto plan = plan_duplicates(analysis.search, analysis.source_inventory, analysis.destination_inventory,
                                      answers, TransferMode::copy, analysis.date_of());
    REQUIRE(plan);
    REQUIRE(plan->deletions.size() == 1);
    CHECK(analysis.path_of(plan->deletions[0].doomed) == "destination:Divers/IMG_0001 (2).JPG");
    CHECK(analysis.path_of(plan->deletions[0].reference) == "destination:2023/12/25/IMG_0001.JPG");
    CHECK(analysis.verify(plan->deletions[0]) == Comparison::identical);
    // Le .xmp de la copie supprimée a un jumeau à côté de la copie gardée.
    CHECK(plan->companion_deletions.size() == 1);
    CHECK(plan->companions_left.empty());
}

TEST_CASE("la copie gardee est celle du dossier de sa date, meme apres les autres")
{
    TempDir card;
    TempDir library;
    write_file(library.path() / "A trier" / "IMG_0001.JPG", photo("2023:12:25 18:04:00"));
    write_file(library.path() / "Z" / "2023" / "12" / "25" / "IMG_0001.JPG", photo("2023:12:25 18:04:00"));

    Analysis analysis(card.path(), library.path());
    REQUIRE(analysis.search.groups.size() == 1);
    const auto kept =
        preselected_destination_copy(analysis.search.groups[0], analysis.destination_inventory, analysis.date_of());
    CHECK(analysis.path_of(kept) == "destination:Z/2023/12/25/IMG_0001.JPG");
}

TEST_CASE("reprise d'un transfert interrompu, sans fichier d'etat")
{
    TempDir card;
    TempDir library;
    const std::vector<std::pair<std::string, std::string>> shots{{"IMG_0001.JPG", "2023:12:25 18:04:00"},
                                                                 {"IMG_0002.JPG", "2023:12:25 18:05:00"},
                                                                 {"IMG_0003.JPG", "2023:12:25 18:06:00"},
                                                                 {"IMG_0004.JPG", "2023:12:25 18:07:00"}};
    for (const auto& [name, date] : shots)
        write_file(card.path() / "DCIM" / "100CANON" / name, photo(date));
    // Les deux premières sont arrivées ; la troisième s'est arrêtée en cours
    // d'écriture, sous son nom temporaire caché.
    write_file(library.path() / "2023" / "12" / "25" / "IMG_0001.JPG", photo("2023:12:25 18:04:00"));
    write_file(library.path() / "2023" / "12" / "25" / "IMG_0002.JPG", photo("2023:12:25 18:05:00"));
    write_file(library.path() / "2023" / "12" / "25" / ".IMG_0003.JPG.tmp", photo("2023:12:25 18:06:00").substr(0, 70000));

    Analysis analysis(card.path(), library.path());
    CHECK(analysis.groups()
          == std::vector<std::vector<std::string>>{
              {"source:DCIM/100CANON/IMG_0001.JPG", "destination:2023/12/25/IMG_0001.JPG"},
              {"source:DCIM/100CANON/IMG_0002.JPG", "destination:2023/12/25/IMG_0002.JPG"}});
    CHECK(analysis.destination_inventory.excluded.size() == 1);
    for (const auto& group : analysis.search.groups)
        CHECK(situations_of(group).questions() == std::vector{DuplicateSituation::between});

    // Relancé en déplacement : les deux déjà arrivées ne repartent pas, et
    // leur original sur la carte ne s'efface qu'après comparaison.
    DuplicateAnswers answers;
    answers.all_between = BetweenChoice::skip_and_delete_source;
    const auto plan = plan_duplicates(analysis.search, analysis.source_inventory, analysis.destination_inventory,
                                      answers, TransferMode::move, analysis.date_of());
    REQUIRE(plan);
    CHECK(plan->not_transferred == std::vector<std::size_t>{0, 1});
    REQUIRE(plan->deletions.size() == 2);
    for (const auto& deletion : plan->deletions)
        CHECK(analysis.verify(deletion) == Comparison::identical);
}

TEST_CASE("une copie abimee en destination n'est pas un doublon et ne se supprime pas")
{
    TempDir card;
    TempDir library;
    write_file(card.path() / "IMG_0001.JPG", photo("2023:12:25 18:04:00"));
    auto damaged = photo("2023:12:25 18:04:00");
    damaged[damaged.size() - 1] = '\x00';
    write_file(library.path() / "2023" / "12" / "25" / "IMG_0001.JPG", damaged);

    Analysis analysis(card.path(), library.path());
    CHECK(analysis.search.groups.empty());

    // Et si le fichier change entre la recherche et la suppression, la
    // comparaison l'arrête.
    write_file(library.path() / "2023" / "12" / "25" / "IMG_0001.JPG", photo("2023:12:25 18:04:00"));
    Analysis again(card.path(), library.path());
    REQUIRE(again.search.groups.size() == 1);
    write_file(library.path() / "2023" / "12" / "25" / "IMG_0001.JPG", damaged);
    const MediaDeletion deletion{{Side::source, 0}, {Side::destination, 0}};
    CHECK(again.verify(deletion) == Comparison::different);
}
