#include "core/implausible_date.hpp"

#include "core/dating.hpp"
#include "core/local_date_time.hpp"

#include <catch2/catch_test_macros.hpp>

#include <vector>

// Noms de tests sans accents : voir version_test.cpp.

using transfertphoto::DateCandidate;
using transfertphoto::DateOrigin;
using transfertphoto::Dating;
using transfertphoto::ImplausibleDateChoice;
using transfertphoto::judge_dates;
using transfertphoto::LocalDateTime;
using transfertphoto::MediaDates;
using transfertphoto::unix_seconds_as_utc;
using transfertphoto::utc_from_unix_seconds;
namespace reason = transfertphoto;

namespace {

const LocalDateTime now{2024, 6, 15, 12, 0, 0, 0};

LocalDateTime day(int year, unsigned month, unsigned d, unsigned hour = 12)
{
    return {year, month, d, hour, 0, 0, 0};
}

// Un fichier daté par ses candidates, la première retenue.
MediaDates dates(std::vector<LocalDateTime> values)
{
    MediaDates result;
    const DateOrigin origins[] = {DateOrigin::metadata, DateOrigin::file_name, DateOrigin::modification};
    for (std::size_t i = 0; i < values.size(); ++i)
        result.candidates.push_back(DateCandidate{values[i], origins[std::min<std::size_t>(i, 2)], true});
    return result;
}

Dating lot(std::vector<MediaDates> media)
{
    Dating dating;
    dating.media = std::move(media);
    return dating;
}

// Un lot ordinaire de count photos en 2023, un jour chacune.
std::vector<MediaDates> ordinary(int count)
{
    std::vector<MediaDates> media;
    for (int i = 0; i < count; ++i)
        media.push_back(dates({day(2023, static_cast<unsigned>(1 + i % 12), static_cast<unsigned>(1 + i % 28))}));
    return media;
}

} // namespace

TEST_CASE("le calendrier se convertit en secondes et retour")
{
    for (const std::int64_t seconds : {0LL, 951'782'400LL, -1LL, -2'082'844'800LL, 4'102'444'799LL}) {
        INFO(seconds);
        CHECK(unix_seconds_as_utc(utc_from_unix_seconds(seconds)) == seconds);
    }
}

TEST_CASE("une date ordinaire est vraisemblable")
{
    const auto verdicts = judge_dates(lot(ordinary(10)), now);
    for (const auto& verdict : verdicts)
        CHECK(verdict.plausible());
}

TEST_CASE("chaque critere joue seul")
{
    SECTION("annee zero")
    {
        const auto verdicts = judge_dates(lot({dates({LocalDateTime{0, 1, 5, 0, 0, 0, 0}})}), now);
        CHECK(verdicts[0].reasons == reason::null_date);
    }
    SECTION("origines connues, a toute heure du 1er janvier")
    {
        for (const int year : {1904, 1970, 1980, 2000}) {
            INFO(year);
            CHECK(judge_dates(lot({dates({day(year, 1, 1, 0)})}), now)[0].reasons == reason::known_origin);
            CHECK(judge_dates(lot({dates({day(year, 1, 1, 23)})}), now)[0].reasons == reason::known_origin);
        }
    }
    SECTION("le 31 decembre precedent l'apres-midi, origine vue de l'ouest")
    {
        // 1970-01-01 00:00 UTC vu de New York : 1969-12-31 19:00.
        CHECK(judge_dates(lot({dates({day(1969, 12, 31, 19)})}), now)[0].reasons == reason::known_origin);
        CHECK(judge_dates(lot({dates({day(1969, 12, 31, 11)})}), now)[0].plausible());
        CHECK(judge_dates(lot({dates({day(2000, 1, 2, 0)})}), now)[0].plausible());
    }
    SECTION("le lendemain est tolere, au-dela non")
    {
        CHECK(judge_dates(lot({dates({LocalDateTime{2024, 6, 16, 11, 0, 0, 0}})}), now)[0].plausible());
        CHECK(judge_dates(lot({dates({LocalDateTime{2024, 6, 16, 12, 0, 0, 0}})}), now)[0].plausible());
        CHECK(judge_dates(lot({dates({LocalDateTime{2024, 6, 16, 12, 0, 1, 0}})}), now)[0].reasons
              == reason::future);
        CHECK(judge_dates(lot({dates({day(2031, 3, 3)})}), now)[0].reasons == reason::future);
    }
    SECTION("groupe isole et ancien")
    {
        auto media = ordinary(10);
        media.push_back(dates({day(2015, 5, 5)}));
        media.push_back(dates({day(2015, 5, 6)}));
        const auto verdicts = judge_dates(lot(media), now);
        CHECK(verdicts[10].reasons == reason::early_group);
        CHECK(verdicts[11].reasons == reason::early_group);
        for (std::size_t i = 0; i < 10; ++i)
            CHECK(verdicts[i].plausible());
    }
}

TEST_CASE("un lot de 2010 a 2024 sans trou n'est pas signale")
{
    std::vector<MediaDates> media;
    for (int year = 2010; year <= 2024; ++year)
        media.push_back(dates({day(year, 3, 1)}));
    for (const auto& verdict : judge_dates(lot(media), now))
        CHECK(verdict.plausible());
}

TEST_CASE("le groupe isole doit etre moins nombreux que le reste")
{
    std::vector<MediaDates> media;
    for (int i = 0; i < 5; ++i)
        media.push_back(dates({day(2015, 5, static_cast<unsigned>(1 + i))}));
    for (int i = 0; i < 5; ++i)
        media.push_back(dates({day(2023, 5, static_cast<unsigned>(1 + i))}));
    for (const auto& verdict : judge_dates(lot(media), now))
        CHECK(verdict.plausible());

    // Quatre en 2015 contre cinq en 2023 : le groupe ancien est le plus petit.
    media.erase(media.begin());
    const auto verdicts = judge_dates(lot(media), now);
    CHECK_FALSE(verdicts[0].plausible());
    CHECK(verdicts[8].plausible());
}

TEST_CASE("un ecart d'un peu moins de deux ans ne separe pas")
{
    auto media = ordinary(10); // de janvier à décembre 2023
    media.push_back(dates({day(2021, 2, 1)})); // moins de deux ans avant le 1er janvier 2023
    for (const auto& verdict : judge_dates(lot(media), now))
        CHECK(verdict.plausible());
}

TEST_CASE("seul le plus ancien groupe est en cause")
{
    // 2015 et 2023 : un vieux dossier, pas une horloge déréglée.
    std::vector<MediaDates> media;
    for (int i = 0; i < 3; ++i)
        media.push_back(dates({day(2005, 5, static_cast<unsigned>(1 + i))}));
    for (int i = 0; i < 20; ++i)
        media.push_back(dates({day(2015, 5, static_cast<unsigned>(1 + i))}));
    for (int i = 0; i < 30; ++i)
        media.push_back(dates({day(2023, 5, static_cast<unsigned>(1 + i % 28))}));
    const auto verdicts = judge_dates(lot(media), now);
    for (std::size_t i = 0; i < 3; ++i)
        CHECK(verdicts[i].reasons == reason::early_group);
    for (std::size_t i = 3; i < verdicts.size(); ++i)
        CHECK(verdicts[i].plausible());
}

TEST_CASE("une horloge jamais reglee qui a continue apres le 1er janvier 2000 est signalee")
{
    auto media = ordinary(50);
    for (unsigned d = 1; d <= 5; ++d)
        media.push_back(dates({day(2000, 1, d * 4 - 3)})); // 1er, 5, 9, 13 et 17 janvier
    const auto verdicts = judge_dates(lot(media), now);
    CHECK(verdicts[50].reasons == reason::known_origin); // le 1er janvier, origine
    for (std::size_t i = 51; i < 55; ++i)
        CHECK(verdicts[i].reasons == reason::early_group);
    for (std::size_t i = 0; i < 50; ++i)
        CHECK(verdicts[i].plausible());
}

TEST_CASE("la date de rechange est la premiere suivante vraisemblable")
{
    auto media = ordinary(10);
    // Métadonnées à l'origine 2000, nom futur, modification ordinaire.
    media.push_back(dates({day(2000, 1, 1), day(2031, 1, 1), day(2023, 8, 8)}));
    // Métadonnées dans le futur, nom ordinaire.
    media.push_back(dates({day(2031, 1, 1), day(2023, 7, 7), day(2023, 8, 8)}));
    // Groupe isolé : une rechange tombant elle aussi avant le reste du lot ne vaut pas.
    media.push_back(dates({day(2015, 5, 5), day(2015, 5, 6), day(2023, 9, 9)}));
    const auto verdicts = judge_dates(lot(media), now);

    REQUIRE(verdicts[10].replacement);
    CHECK(*verdicts[10].replacement == 2);
    REQUIRE(verdicts[11].replacement);
    CHECK(*verdicts[11].replacement == 1);
    CHECK(verdicts[12].reasons == reason::early_group);
    REQUIRE(verdicts[12].replacement);
    CHECK(*verdicts[12].replacement == 2);
}

TEST_CASE("le choix preselectionne est la rechange, sinon __A_TRIER__")
{
    auto media = ordinary(10);
    media.push_back(dates({day(2000, 1, 1), day(2023, 8, 8)}));
    media.push_back(dates({day(2000, 1, 1), day(2000, 1, 1, 15)})); // l'appareil a tout faussé
    const auto verdicts = judge_dates(lot(media), now);

    CHECK(verdicts[10].preselected() == ImplausibleDateChoice::use_next);
    CHECK(verdicts[10].choices()
          == std::vector{ImplausibleDateChoice::use_next, ImplausibleDateChoice::keep, ImplausibleDateChoice::to_sort,
                         ImplausibleDateChoice::skip});

    CHECK_FALSE(verdicts[11].replacement);
    CHECK(verdicts[11].preselected() == ImplausibleDateChoice::to_sort);
    CHECK(verdicts[11].choices()
          == std::vector{ImplausibleDateChoice::keep, ImplausibleDateChoice::to_sort, ImplausibleDateChoice::skip});
    CHECK(transfertphoto::to_sort_folder == "__A_TRIER__");
}

TEST_CASE("un lot vide ou d'un seul fichier se juge sans peine")
{
    CHECK(judge_dates(lot({}), now).empty());
    const auto verdicts = judge_dates(lot({dates({day(2015, 5, 5)})}), now);
    REQUIRE(verdicts.size() == 1);
    CHECK(verdicts[0].plausible());
}
