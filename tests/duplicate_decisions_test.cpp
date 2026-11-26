#include "core/duplicate_decisions.hpp"

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <vector>

// Noms de tests sans accents : voir version_test.cpp.

using namespace transfertphoto;

namespace {

// Source et destination décrites par leurs chemins, chaque photo avec ses
// accompagnants ; les groupes se donnent à la main.
struct Fixture {
    Inventory source;
    Inventory destination;
    DuplicateSearch search;

    FileRef add(Side side, const std::string& path, std::vector<std::pair<std::string, CompanionKind>> companions = {})
    {
        auto& inventory = side == Side::source ? source : destination;
        MediaItem item;
        item.entry.relative_path = path;
        item.entry.size = 1000;
        for (const auto& [companion, kind] : companions) {
            CompanionFile file;
            file.entry.relative_path = companion;
            file.kind = kind;
            item.companions.push_back(file);
        }
        inventory.media.push_back(item);
        return {side, inventory.media.size() - 1};
    }

    void group(std::vector<FileRef> members)
    {
        DuplicateGroup g;
        g.members = std::move(members);
        g.size = 1000;
        search.groups.push_back(g);
    }

    std::optional<DuplicatePlan> plan(const DuplicateAnswers& answers, TransferMode mode,
                                      const DestinationDate& date_of = {}) const
    {
        return plan_duplicates(search, source, destination, answers, mode, date_of);
    }
};

using enum Side;

} // namespace

TEST_CASE("les choix offerts dependent du mode")
{
    CHECK(between_choices(TransferMode::copy) == std::vector{BetweenChoice::skip, BetweenChoice::transfer_renamed});
    CHECK(between_choices(TransferMode::move)
          == std::vector{BetweenChoice::skip, BetweenChoice::skip_and_delete_source, BetweenChoice::transfer_renamed});
    CHECK(within_source_choices(TransferMode::copy)
          == std::vector{WithinSourceChoice::transfer_one, WithinSourceChoice::transfer_all});
    CHECK(within_source_choices(TransferMode::move)
          == std::vector{WithinSourceChoice::transfer_one, WithinSourceChoice::transfer_one_delete_others,
                         WithinSourceChoice::transfer_all});
    CHECK(within_destination_choices()
          == std::vector{WithinDestinationChoice::keep_one, WithinDestinationChoice::leave});
}

TEST_CASE("les questions en suspens, la reponse propre et appliquer a tous")
{
    Fixture f;
    const auto a = f.add(source, "a.jpg");
    const auto b = f.add(source, "b.jpg");
    const auto c = f.add(source, "c.jpg");
    const auto d = f.add(destination, "2023/01/01/c.jpg");
    const auto e = f.add(destination, "x/e.jpg");
    const auto g2 = f.add(destination, "y/e.jpg");
    f.group({a, b});        // dans la source
    f.group({c, d});        // entre les deux
    f.group({e, g2});       // dans la destination

    DuplicateAnswers answers;
    CHECK(pending_questions(f.search, answers, TransferMode::copy)
          == std::vector<PendingQuestion>{{0, DuplicateSituation::within_source},
                                          {1, DuplicateSituation::between},
                                          {2, DuplicateSituation::within_destination}});
    CHECK_FALSE(f.plan(answers, TransferMode::copy));

    // Une réponse propre au groupe 0, « appliquer à tous » pour les situations entre les deux.
    answers.groups.resize(1);
    answers.groups[0].within_source = WithinSourceChoice::transfer_all;
    answers.all_between = BetweenChoice::skip;
    CHECK(pending_questions(f.search, answers, TransferMode::copy)
          == std::vector<PendingQuestion>{{2, DuplicateSituation::within_destination}});

    answers.all_within_destination = WithinDestinationChoice::leave;
    CHECK(pending_questions(f.search, answers, TransferMode::copy).empty());
    CHECK(f.plan(answers, TransferMode::copy));
}

TEST_CASE("la reponse propre l'emporte sur appliquer a tous")
{
    Fixture f;
    const auto a = f.add(source, "a.jpg");
    const auto b = f.add(destination, "a.jpg");
    const auto c = f.add(source, "c.jpg");
    const auto d = f.add(destination, "c.jpg");
    f.group({a, b});
    f.group({c, d});

    DuplicateAnswers answers;
    answers.all_between = BetweenChoice::skip;
    answers.groups.resize(2);
    answers.groups[1].between = BetweenChoice::transfer_renamed;
    const auto plan = f.plan(answers, TransferMode::copy);
    REQUIRE(plan);
    CHECK(plan->not_transferred == std::vector<std::size_t>{0});
    CHECK(plan->transfer_renamed == std::vector<std::size_t>{1});
}

TEST_CASE("un choix de deplacement reste en suspens en mode copie")
{
    Fixture f;
    const auto a = f.add(source, "a.jpg");
    const auto b = f.add(destination, "a.jpg");
    f.group({a, b});
    DuplicateAnswers answers;
    answers.all_between = BetweenChoice::skip_and_delete_source;
    CHECK(pending_questions(f.search, answers, TransferMode::copy).size() == 1);
    CHECK(pending_questions(f.search, answers, TransferMode::move).empty());
}

TEST_CASE("entre source et destination : ne pas retransferer, sans rien supprimer")
{
    Fixture f;
    const auto a = f.add(source, "DCIM/a.jpg", {{"DCIM/a.xmp", CompanionKind::xmp}});
    const auto b = f.add(source, "DCIM/b.jpg");
    const auto d = f.add(destination, "2023/01/01/a.jpg");
    f.group({a, b, d});
    DuplicateAnswers answers;
    answers.all_between = BetweenChoice::skip;
    for (const auto mode : {TransferMode::copy, TransferMode::move}) {
        const auto plan = f.plan(answers, mode);
        REQUIRE(plan);
        CHECK(plan->not_transferred == std::vector<std::size_t>{0, 1});
        CHECK(plan->deletions.empty());
        CHECK(plan->companion_deletions.empty());
        CHECK(plan->companions_left.empty());
    }
}

TEST_CASE("entre source et destination : supprimer la source et ses accompagnants jumeaux")
{
    Fixture f;
    const auto a = f.add(source, "DCIM/IMG_0001.HEIC",
                         {{"DCIM/IMG_0001.AAE", CompanionKind::aae}, {"DCIM/IMG_0001.xmp", CompanionKind::xmp}});
    const auto d = f.add(destination, "2023/01/01/IMG_0001.HEIC", {{"2023/01/01/IMG_0001.xmp", CompanionKind::xmp}});
    f.group({a, d});
    DuplicateAnswers answers;
    answers.all_between = BetweenChoice::skip_and_delete_source;

    const auto plan = f.plan(answers, TransferMode::move);
    REQUIRE(plan);
    CHECK(plan->not_transferred == std::vector<std::size_t>{0});
    CHECK(plan->deletions == std::vector<MediaDeletion>{{a, d}});
    // Le .xmp a son jumeau à côté de la copie en destination ; le .aae non.
    CHECK(plan->companion_deletions == std::vector<CompanionDeletion>{{{a, 1}, {d, 0}}});
    CHECK(plan->companions_left == std::vector<CompanionRef>{{a, 0}});
}

TEST_CASE("entre source et destination : transferer sous un autre nom")
{
    Fixture f;
    const auto a = f.add(source, "a.jpg");
    const auto d = f.add(destination, "a.jpg");
    f.group({a, d});
    DuplicateAnswers answers;
    answers.all_between = BetweenChoice::transfer_renamed;
    const auto plan = f.plan(answers, TransferMode::move);
    REQUIRE(plan);
    CHECK(plan->transfer_renamed == std::vector<std::size_t>{0});
    CHECK(plan->not_transferred.empty());
    CHECK(plan->deletions.empty());
}

TEST_CASE("dans la source : n'en transferer qu'une, laisser ou supprimer les autres")
{
    Fixture f;
    const auto a = f.add(source, "100/a.jpg", {{"100/a.xmp", CompanionKind::xmp}});
    const auto b = f.add(source, "101/a.jpg", {{"101/a.xmp", CompanionKind::xmp}, {"101/a.aae", CompanionKind::aae}});
    const auto c = f.add(source, "102/a.jpg");
    f.group({a, b, c});

    SECTION("laisser : la premiere est transferee, rien n'est supprime")
    {
        DuplicateAnswers answers;
        answers.all_within_source = WithinSourceChoice::transfer_one;
        const auto plan = f.plan(answers, TransferMode::copy);
        REQUIRE(plan);
        CHECK(plan->not_transferred == std::vector<std::size_t>{1, 2});
        CHECK(plan->deletions.empty());
    }
    SECTION("supprimer, avec la copie choisie")
    {
        DuplicateAnswers answers;
        answers.groups.resize(1);
        answers.groups[0].within_source = WithinSourceChoice::transfer_one_delete_others;
        answers.groups[0].source_kept = c;
        const auto plan = f.plan(answers, TransferMode::move);
        REQUIRE(plan);
        CHECK(plan->not_transferred == std::vector<std::size_t>{0, 1});
        CHECK(plan->deletions == std::vector<MediaDeletion>{{a, c}, {b, c}});
        // La copie gardée n'a pas d'accompagnant : aucun jumeau, tout reste.
        CHECK(plan->companion_deletions.empty());
        CHECK(plan->companions_left == std::vector<CompanionRef>{{a, 0}, {b, 0}, {b, 1}});
    }
    SECTION("tout transferer")
    {
        DuplicateAnswers answers;
        answers.all_within_source = WithinSourceChoice::transfer_all;
        const auto plan = f.plan(answers, TransferMode::move);
        REQUIRE(plan);
        CHECK(plan->not_transferred.empty());
        CHECK(plan->deletions.empty());
    }
}

TEST_CASE("dans la destination : n'en garder qu'une, celle du dossier de sa date")
{
    Fixture f;
    const auto x = f.add(destination, "Divers/IMG_0001.JPG", {{"Divers/IMG_0001.xmp", CompanionKind::xmp}});
    const auto y = f.add(destination, "2023/12/25/IMG_0001.JPG", {{"2023/12/25/IMG_0001.xmp", CompanionKind::xmp}});
    const auto z = f.add(destination, "Vrac/IMG_0001.JPG", {{"Vrac/IMG_0001.AAE", CompanionKind::aae}});
    f.group({x, y, z});
    const DestinationDate christmas = [](std::size_t) { return LocalDateTime{2023, 12, 25, 18, 4, 0, 0}; };

    CHECK(preselected_destination_copy(f.search.groups[0], f.destination, christmas) == y);
    CHECK(preselected_destination_copy(f.search.groups[0], f.destination, {}) == x);

    DuplicateAnswers answers;
    answers.all_within_destination = WithinDestinationChoice::keep_one;
    const auto plan = f.plan(answers, TransferMode::copy, christmas);
    REQUIRE(plan);
    CHECK(plan->deletions == std::vector<MediaDeletion>{{x, y}, {z, y}});
    CHECK(plan->companion_deletions == std::vector<CompanionDeletion>{{{x, 0}, {y, 0}}});
    CHECK(plan->companions_left == std::vector<CompanionRef>{{z, 0}});

    answers.all_within_destination = WithinDestinationChoice::leave;
    const auto left = f.plan(answers, TransferMode::copy, christmas);
    REQUIRE(left);
    CHECK(left->deletions.empty());
}

TEST_CASE("le dossier de la date se reconnait a un element entier du chemin")
{
    Fixture f;
    const auto x = f.add(destination, "A/12023/12/25/a.jpg");
    const auto y = f.add(destination, "__A_TRIER__/2023/12/25/a.jpg");
    const auto z = f.add(destination, "2023/12/25/sous/a.jpg");
    f.group({x, y, z});
    const DestinationDate christmas = [](std::size_t) { return LocalDateTime{2023, 12, 25, 0, 0, 0, 0}; };
    CHECK(preselected_destination_copy(f.search.groups[0], f.destination, christmas) == y);
}

TEST_CASE("groupe mele : la source supprimee se compare a la copie gardee")
{
    Fixture f;
    const auto a = f.add(source, "DCIM/a.jpg", {{"DCIM/a.xmp", CompanionKind::xmp}});
    const auto x = f.add(destination, "Vrac/a.jpg", {{"Vrac/a.xmp", CompanionKind::xmp}});
    const auto y = f.add(destination, "2023/12/25/a.jpg");
    f.group({a, x, y});
    const DestinationDate christmas = [](std::size_t) { return LocalDateTime{2023, 12, 25, 0, 0, 0, 0}; };

    DuplicateAnswers answers;
    answers.all_between = BetweenChoice::skip_and_delete_source;
    answers.all_within_destination = WithinDestinationChoice::keep_one;
    const auto plan = f.plan(answers, TransferMode::move, christmas);
    REQUIRE(plan);
    // Gardée : y, dans le dossier de sa date ; x part, son .xmp sans jumeau reste.
    CHECK(plan->deletions == std::vector<MediaDeletion>{{x, y}, {a, y}});
    // Le .xmp de la source n'a pas de jumeau à côté de y (celui de x part).
    CHECK(plan->companion_deletions.empty());
    CHECK(plan->companions_left == std::vector<CompanionRef>{{x, 0}, {a, 0}});

    // Si l'on laisse la destination telle quelle, le .xmp de x sert de jumeau.
    answers.all_within_destination = WithinDestinationChoice::leave;
    const auto kept_all = f.plan(answers, TransferMode::move, christmas);
    REQUIRE(kept_all);
    CHECK(kept_all->deletions == std::vector<MediaDeletion>{{a, y}});
    CHECK(kept_all->companion_deletions == std::vector<CompanionDeletion>{{{a, 0}, {x, 0}}});
}
