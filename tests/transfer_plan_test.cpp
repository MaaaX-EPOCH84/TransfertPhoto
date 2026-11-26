#include "core/transfer_plan.hpp"

#include "test_files.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

// Noms de tests sans accents : voir version_test.cpp.

using namespace transfertphoto;

namespace {

const LocalDateTime christmas{2023, 12, 25, 18, 4, 0, 0};
const LocalDateTime origin{2000, 1, 1, 0, 0, 0, 0};

// La source, ses dates et leur jugement, construits ensemble ; une
// destination ; des doublons donnés à la main.
struct Fixture {
    Inventory source;
    Dating dating;
    std::vector<DateVerdict> verdicts;
    DateAnswers date_answers;
    Inventory destination;
    DuplicateSearch duplicates;
    DuplicateAnswers duplicate_answers;
    TransferOptions options;

    std::size_t add(const std::string& path, std::uint64_t size, std::vector<DateCandidate> candidates,
                    std::vector<std::pair<std::string, std::uint64_t>> companions = {}, DateVerdict verdict = {})
    {
        MediaItem item;
        item.entry.relative_path = path;
        item.entry.size = size;
        for (const auto& [companion, companion_size] : companions) {
            CompanionFile file;
            file.entry.relative_path = companion;
            file.entry.size = companion_size;
            item.companions.push_back(file);
        }
        source.media.push_back(item);
        MediaDates dates;
        dates.candidates = std::move(candidates);
        dating.media.push_back(dates);
        verdicts.push_back(verdict);
        return source.media.size() - 1;
    }

    void existing(const std::string& path)
    {
        MediaItem item;
        item.entry.relative_path = path;
        destination.media.push_back(item);
    }

    TransferPlan plan(std::optional<std::uint64_t> free_space = std::nullopt) const
    {
        return plan_transfer(source, dating, verdicts, date_answers, destination, duplicates, duplicate_answers,
                             options, {}, free_space);
    }
};

DateCandidate from_exif(const LocalDateTime& value)
{
    return {value, DateOrigin::metadata, true};
}

DateVerdict implausible(std::optional<std::size_t> replacement)
{
    DateVerdict verdict;
    verdict.reasons = known_origin;
    verdict.replacement = replacement;
    return verdict;
}

} // namespace

TEST_CASE("chaque photo recoit son dossier et son nom, avec ses accompagnants")
{
    Fixture f;
    f.add("DCIM/IMG_0001.HEIC", 1000, {from_exif(christmas)}, {{"DCIM/IMG_0001.MOV", 300}, {"DCIM/IMG_0001.AAE", 2}});
    f.add("DCIM/IMG_0002.JPG", 500, {from_exif(LocalDateTime{2024, 1, 2, 9, 0, 0, 0})});
    f.existing("2023/12/25/IMG_0001.HEIC"); // une autre photo du même nom

    const auto plan = f.plan();
    REQUIRE(plan.ready());
    REQUIRE(plan.operations.size() == 2);
    const auto& first = plan.operations[0];
    CHECK(first.folder == "2023/12/25");
    CHECK(first.name == "IMG_0001 (2).HEIC");
    CHECK(first.companion_names == std::vector<std::string>{"IMG_0001 (2).MOV", "IMG_0001 (2).AAE"});
    CHECK(first.bytes == 1302);
    CHECK(first.origin == DateOrigin::metadata);
    CHECK(plan.operations[1].folder == "2024/01/02");
    CHECK(plan.operations[1].name == "IMG_0002.JPG");
    CHECK(plan.bytes_to_write == 1802);
    CHECK(plan.companions == 2);
}

TEST_CASE("classement decoche, tout va a la racine sans se rencontrer")
{
    Fixture f;
    f.options.sort_by_date = false;
    f.add("100/IMG_0001.JPG", 10, {from_exif(christmas)});
    f.add("101/IMG_0001.JPG", 10, {from_exif(christmas)});
    const auto plan = f.plan();
    REQUIRE(plan.operations.size() == 2);
    CHECK(plan.operations[0].folder.empty());
    CHECK(plan.operations[0].name == "IMG_0001.JPG");
    CHECK(plan.operations[1].name == "IMG_0001 (2).JPG");
}

TEST_CASE("une date invraisemblable sans choix bloque le plan")
{
    Fixture f;
    f.add("IMG_0001.JPG", 10, {from_exif(origin), {christmas, DateOrigin::modification, true}}, {}, implausible(1));
    const auto pending = f.plan();
    CHECK_FALSE(pending.ready());
    CHECK(pending.pending_dates == std::vector<std::size_t>{0});
    CHECK(pending.operations.empty());

    f.date_answers.all = ImplausibleDateChoice::use_next;
    const auto plan = f.plan();
    REQUIRE(plan.ready());
    CHECK(plan.operations[0].folder == "2023/12/25");
    CHECK(plan.operations[0].origin == DateOrigin::modification);
}

TEST_CASE("le choix propre a une photo l'emporte sur appliquer a tous")
{
    Fixture f;
    f.add("IMG_0001.JPG", 10, {from_exif(origin)}, {}, implausible(std::nullopt));
    f.add("IMG_0002.JPG", 10, {from_exif(origin)}, {}, implausible(std::nullopt));
    f.date_answers.all = ImplausibleDateChoice::to_sort;
    f.date_answers.media = {ImplausibleDateChoice::skip};
    const auto plan = f.plan();
    REQUIRE(plan.ready());
    CHECK(plan.skipped_for_date == std::vector<std::size_t>{0});
    REQUIRE(plan.operations.size() == 1);
    CHECK(plan.operations[0].folder == "__A_TRIER__/2000/01/01");
}

TEST_CASE("les doublons sans reponse bloquent le plan, et leurs reponses le reglent")
{
    Fixture f;
    f.add("DCIM/a.jpg", 10, {from_exif(christmas)});
    f.add("DCIM/b.jpg", 10, {from_exif(christmas)});
    f.add("DCIM/c.jpg", 10, {from_exif(christmas)});
    f.existing("2023/12/25/c.jpg");
    f.duplicates.groups.push_back({{{Side::source, 0}, {Side::source, 1}}, 10, {}});
    f.duplicates.groups.push_back({{{Side::source, 2}, {Side::destination, 0}}, 10, {}});

    const auto pending = f.plan();
    CHECK(pending.pending_duplicates.size() == 2);
    CHECK(pending.operations.empty());

    f.duplicate_answers.all_within_source = WithinSourceChoice::transfer_one;
    f.duplicate_answers.all_between = BetweenChoice::skip;
    const auto plan = f.plan();
    REQUIRE(plan.ready());
    REQUIRE(plan.operations.size() == 1);
    CHECK(plan.operations[0].name == "a.jpg");
    CHECK(plan.duplicates.not_transferred == std::vector<std::size_t>{1, 2});

    // Transférer quand même sous un autre nom : la copie identique occupe le sien.
    f.duplicate_answers.all_between = BetweenChoice::transfer_renamed;
    const auto renamed = f.plan();
    REQUIRE(renamed.operations.size() == 2);
    CHECK(renamed.operations[1].name == "c (2).jpg");
}

TEST_CASE("l'espace libre se compare aux octets a ecrire")
{
    Fixture f;
    f.add("IMG_0001.JPG", 1000, {from_exif(christmas)}, {{"IMG_0001.xmp", 24}});
    CHECK(f.plan(1024).ready());
    CHECK(f.plan(1024).enough_space());
    CHECK_FALSE(f.plan(1023).enough_space());
    CHECK_FALSE(f.plan(1023).ready());
    CHECK(f.plan(std::nullopt).ready());
}

TEST_CASE("l'espace libre d'une destination pas encore creee est celui de son parent")
{
    test_support::TempDir root;
    const auto space = free_space_at(root.path() / "Photos" / "2023");
    REQUIRE(space);
    CHECK(*space > 0);
}
