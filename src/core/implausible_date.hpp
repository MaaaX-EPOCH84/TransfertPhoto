#pragma once

#include "core/dating.hpp"
#include "core/local_date_time.hpp"

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace transfertphoto {

// Pourquoi une date est invraisemblable ; plusieurs raisons peuvent valoir.
enum ImplausibleReason : unsigned {
    null_date = 1u << 0,    // année zéro
    known_origin = 1u << 1, // 1er janvier 1904, 1970, 1980 ou 2000
    future = 1u << 2,       // plus d'un jour après maintenant
    early_group = 1u << 3,  // groupe isolé, antérieur de deux ans au reste
};

// Ce qu'on propose devant une date invraisemblable, chaque choix valant pour
// un fichier ou, par « appliquer à tous », pour le transfert en cours.
enum class ImplausibleDateChoice {
    use_next, // la date suivante vraisemblable dans l'ordre de repli
    keep,     // la date telle quelle
    to_sort,  // le dossier __A_TRIER__, la date gardée pour ses sous-dossiers
    skip,     // ne pas transférer
};

// À la racine de la destination. Classement coché, le fichier y va dans
// AAAA/MM/JJ selon sa date même fausse ; décoché, à plat.
inline constexpr std::string_view to_sort_folder = "__A_TRIER__";

struct DateVerdict {
    unsigned reasons = 0; // ImplausibleReason combinées ; 0 = vraisemblable
    // Rang, dans MediaDates::candidates, de la première date suivante qui
    // est vraisemblable ; rien s'il n'y en a pas.
    std::optional<std::size_t> replacement;

    [[nodiscard]] bool plausible() const noexcept { return reasons == 0; }
    // Le choix présélectionné dans la fenêtre : la date de rechange s'il y en
    // a une, sinon __A_TRIER__.
    [[nodiscard]] ImplausibleDateChoice preselected() const noexcept;
    // Les choix à proposer, dans l'ordre : « date suivante » seulement s'il y
    // en a une.
    [[nodiscard]] std::vector<ImplausibleDateChoice> choices() const;
};

// Écart minimal entre un groupe isolé et le reste du lot : deux ans.
inline constexpr std::int64_t early_group_gap_seconds = 730 * 86'400 + 43'200;

// Juge la date retenue de chaque photo et vidéo, au rang de dating.media :
//
//   - année zéro ;
//   - le 1er janvier 1904 (QuickTime), 1970 (Unix), 1980 (FAT) ou 2000
//     (appareil jamais réglé), à toute heure, et le 31 décembre qui précède à
//     partir de midi : le même instant d'origine, vu d'un fuseau à l'ouest ;
//   - plus d'un jour après now, l'heure locale de la machine ;
//   - le plus ancien groupe du lot, s'il est séparé du suivant par deux ans
//     au moins et moins nombreux que tout le reste. Les dates déjà jugées
//     invraisemblables pour une autre raison n'entrent pas dans ce calcul.
//
// Une date de rechange doit échapper aux trois premiers critères et, quand le
// lot a un groupe isolé, ne pas tomber avant le reste du lot.
[[nodiscard]] std::vector<DateVerdict> judge_dates(const Dating& dating, const LocalDateTime& now);

} // namespace transfertphoto
