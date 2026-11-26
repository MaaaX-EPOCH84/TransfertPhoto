#include "core/destination_path.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

// Noms de tests sans accents : voir version_test.cpp.

using namespace transfertphoto;

namespace {

MediaDates dates(std::vector<LocalDateTime> values)
{
    MediaDates result;
    for (const auto& value : values)
        result.candidates.push_back(DateCandidate{value, DateOrigin::metadata, true});
    return result;
}

const LocalDateTime christmas{2023, 12, 25, 18, 4, 0, 0};
const LocalDateTime origin{2000, 1, 1, 0, 0, 0, 0};

DateVerdict implausible(std::optional<std::size_t> replacement)
{
    DateVerdict verdict;
    verdict.reasons = known_origin;
    verdict.replacement = replacement;
    return verdict;
}

} // namespace

TEST_CASE("une date vraisemblable donne son dossier AAAA/MM/JJ, ou la racine")
{
    const auto placed = place(dates({christmas}), DateVerdict{}, std::nullopt, true);
    CHECK(placed.kind == Placement::Kind::folder);
    CHECK(placed.folder == "2023/12/25");
    CHECK(place(dates({christmas}), DateVerdict{}, std::nullopt, false).folder.empty());
    CHECK(date_folder(LocalDateTime{987, 1, 2, 0, 0, 0, 0}) == "0987/01/02");
}

TEST_CASE("devant une date invraisemblable, le choix decide du dossier")
{
    const auto two = dates({origin, christmas});

    SECTION("sans choix, la fenetre doit demander")
    {
        CHECK(place(two, implausible(1), std::nullopt, true).kind == Placement::Kind::pending);
    }
    SECTION("la date suivante")
    {
        const auto placed = place(two, implausible(1), ImplausibleDateChoice::use_next, true);
        CHECK(placed.kind == Placement::Kind::folder);
        CHECK(placed.folder == "2023/12/25");
        CHECK(placed.date == christmas);
    }
    SECTION("la date suivante sans rechange reste en suspens")
    {
        CHECK(place(dates({origin}), implausible(std::nullopt), ImplausibleDateChoice::use_next, true).kind
              == Placement::Kind::pending);
    }
    SECTION("la date gardee")
    {
        CHECK(place(two, implausible(1), ImplausibleDateChoice::keep, true).folder == "2000/01/01");
    }
    SECTION("__A_TRIER__, avec la date meme fausse, ou a plat")
    {
        CHECK(place(two, implausible(1), ImplausibleDateChoice::to_sort, true).folder == "__A_TRIER__/2000/01/01");
        CHECK(place(two, implausible(1), ImplausibleDateChoice::to_sort, false).folder == "__A_TRIER__");
    }
    SECTION("ne pas transferer")
    {
        CHECK(place(two, implausible(1), ImplausibleDateChoice::skip, true).kind == Placement::Kind::skip);
    }
    SECTION("classement decoche : la date suivante va a la racine")
    {
        CHECK(place(two, implausible(1), ImplausibleDateChoice::use_next, false).folder.empty());
    }
}

TEST_CASE("un nom libre reste tel quel, un nom pris recoit un numero")
{
    NameAllocator names({"2023/12/25/IMG_0001.JPG", "2023/12/25/IMG_0002.JPG", "2023/12/25/IMG_0002 (2).JPG"});
    CHECK(names.allocate("2023/12/25", "IMG_0003.JPG", {}).name == "IMG_0003.JPG");
    CHECK(names.allocate("2023/12/25", "IMG_0001.JPG", {}).name == "IMG_0001 (2).JPG");
    CHECK(names.allocate("2023/12/25", "IMG_0002.JPG", {}).name == "IMG_0002 (3).JPG");
    // Le même nom dans un autre dossier est libre.
    CHECK(names.allocate("2023/12/26", "IMG_0001.JPG", {}).name == "IMG_0001.JPG");
    // À la racine.
    CHECK(names.allocate("", "IMG_0001.JPG", {}).name == "IMG_0001.JPG");
}

TEST_CASE("deux fichiers du meme transfert ne prennent jamais le meme nom")
{
    NameAllocator names;
    CHECK(names.allocate("2023/12/25", "IMG_0001.JPG", {}).name == "IMG_0001.JPG");
    CHECK(names.allocate("2023/12/25", "IMG_0001.JPG", {}).name == "IMG_0001 (2).JPG");
    CHECK(names.allocate("2023/12/25", "IMG_0001.JPG", {}).name == "IMG_0001 (3).JPG");
    CHECK(names.is_taken("2023/12/25", "IMG_0001 (3).JPG"));
}

TEST_CASE("la casse ne distingue pas deux noms")
{
    NameAllocator names({"2023/12/25/img_0001.jpg"});
    CHECK(names.allocate("2023/12/25", "IMG_0001.JPG", {}).name == "IMG_0001 (2).JPG");
}

TEST_CASE("les accompagnants prennent le numero de leur photo")
{
    NameAllocator names({"2023/12/25/IMG_0001.HEIC"});
    const auto allocated = names.allocate("2023/12/25", "IMG_0001.HEIC",
                                          {"IMG_0001.MOV", "IMG_0001.AAE", "IMG_0001.HEIC.xmp", "IMG_O0001.AAE"});
    CHECK(allocated.name == "IMG_0001 (2).HEIC");
    CHECK(allocated.companions
          == std::vector<std::string>{"IMG_0001 (2).MOV", "IMG_0001 (2).AAE", "IMG_0001 (2).HEIC.xmp",
                                      "IMG_O0001 (2).AAE"});
}

TEST_CASE("un accompagnant deja pris fait passer toute la photo au numero suivant")
{
    // La photo est libre, mais un .xmp orphelin porte déjà son nom.
    NameAllocator names({"2023/12/25/IMG_0001.xmp", "2023/12/25/IMG_0001 (2).MOV"});
    const auto allocated = names.allocate("2023/12/25", "IMG_0001.JPG", {"IMG_0001.xmp", "IMG_0001.MOV"});
    CHECK(allocated.name == "IMG_0001 (3).JPG");
    CHECK(allocated.companions == std::vector<std::string>{"IMG_0001 (3).xmp", "IMG_0001 (3).MOV"});
}

TEST_CASE("le debut commun d'un accompagnant doit s'arreter a un point")
{
    CHECK(renamed_companion("IMG_00012.xmp", "IMG_0001.JPG", "IMG_0001 (2).JPG", 2) == "IMG_00012 (2).xmp");
    CHECK(renamed_companion("img_0001.xmp", "IMG_0001.JPG", "IMG_0001 (2).JPG", 2) == "IMG_0001 (2).xmp");
    CHECK(renamed_companion("IMG_0001.xmp", "IMG_0001.JPG", "IMG_0001.JPG", 1) == "IMG_0001.xmp");
    CHECK(renamed_companion("SANS_POINT", "IMG_0001.JPG", "IMG_0001 (2).JPG", 2) == "SANS_POINT (2)");
}

TEST_CASE("tous les chemins d'une destination comptent comme pris")
{
    Inventory destination;
    MediaItem item;
    item.entry.relative_path = "2023/IMG_0001.JPG";
    CompanionFile xmp;
    xmp.entry.relative_path = "2023/IMG_0001.xmp";
    item.companions.push_back(xmp);
    destination.media.push_back(item);
    SourceEntry orphan;
    orphan.relative_path = "2023/IMG_0009.xmp";
    destination.orphan_companions.push_back(orphan);
    SourceEntry hidden;
    hidden.relative_path = "2023/.IMG_0002.JPG.transfertphoto";
    destination.excluded.push_back(hidden);
    SourceEntry text;
    text.relative_path = "2023/notes.txt";
    destination.ignored.push_back(text);

    CHECK(existing_paths(destination)
          == std::vector<std::string>{"2023/IMG_0001.JPG", "2023/IMG_0001.xmp", "2023/IMG_0009.xmp",
                                      "2023/.IMG_0002.JPG.transfertphoto", "2023/notes.txt"});
}
