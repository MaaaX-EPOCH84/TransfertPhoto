#pragma once

#include "core/duplicate_situations.hpp"
#include "core/duplicates.hpp"
#include "core/inventory.hpp"
#include "core/local_date_time.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

namespace transfertphoto {

enum class TransferMode {
    copy,
    move,
};

// Entre source et destination : la photo est déjà arrivée.
enum class BetweenChoice {
    skip,                   // ne pas retransférer
    skip_and_delete_source, // ne pas retransférer, et supprimer la source (déplacement seulement)
    transfer_renamed,       // transférer quand même, sous un autre nom
};

// Plusieurs copies dans la source, aucune en destination.
enum class WithinSourceChoice {
    transfer_one,               // n'en transférer qu'une, laisser les autres
    transfer_one_delete_others, // n'en transférer qu'une, supprimer les autres (déplacement seulement)
    transfer_all,
};

// Plusieurs copies dans la destination.
enum class WithinDestinationChoice {
    keep_one, // supprimer les autres
    leave,
};

// Les choix à proposer, dans l'ordre, selon le mode.
[[nodiscard]] std::vector<BetweenChoice> between_choices(TransferMode mode);
[[nodiscard]] std::vector<WithinSourceChoice> within_source_choices(TransferMode mode);
[[nodiscard]] std::vector<WithinDestinationChoice> within_destination_choices();

// Les réponses données pour un groupe.
struct GroupAnswers {
    std::optional<BetweenChoice> between;
    std::optional<WithinSourceChoice> within_source;
    std::optional<WithinDestinationChoice> within_destination;
    // La copie transférée (dans la source) ou gardée (dans la destination) ;
    // à défaut, celle que présélectionne la fenêtre.
    std::optional<FileRef> source_kept;
    std::optional<FileRef> destination_kept;
};

// Toutes les réponses d'un transfert. « Appliquer à tous » vaut pour les
// groupes sans réponse propre de la même situation, et pour ce transfert
// seulement : rien n'en est gardé.
struct DuplicateAnswers {
    std::vector<GroupAnswers> groups; // au rang de DuplicateSearch::groups ; peut être plus court
    std::optional<BetweenChoice> all_between;
    std::optional<WithinSourceChoice> all_within_source;
    std::optional<WithinDestinationChoice> all_within_destination;
};

struct PendingQuestion {
    std::size_t group = 0;
    DuplicateSituation situation = DuplicateSituation::between;

    friend bool operator==(const PendingQuestion&, const PendingQuestion&) = default;
};

// Les questions encore sans réponse, ou dont la réponse n'est pas offerte dans
// ce mode, groupe par groupe.
[[nodiscard]] std::vector<PendingQuestion> pending_questions(const DuplicateSearch& search,
                                                             const DuplicateAnswers& answers, TransferMode mode);

// La date d'une photo ou d'une vidéo de la destination, si on la connaît.
using DestinationDate = std::function<std::optional<LocalDateTime>(std::size_t destination_index)>;

// Copie présélectionnée dans « n'en garder qu'une » : celle qui est déjà dans
// le dossier de sa date (…/AAAA/MM/JJ/), sinon la première dans l'ordre des
// chemins.
[[nodiscard]] FileRef preselected_destination_copy(const DuplicateGroup& group,
                                                   const Inventory& destination_inventory,
                                                   const DestinationDate& date_of);

// Copie présélectionnée dans « n'en transférer qu'une » : la première de la
// source dans l'ordre des chemins.
[[nodiscard]] FileRef preselected_source_copy(const DuplicateGroup& group);

// Un accompagnant, par son porteur et son rang parmi ses accompagnants.
struct CompanionRef {
    FileRef carrier;
    std::size_t index = 0;

    friend bool operator==(const CompanionRef&, const CompanionRef&) = default;
};

// Une photo ou une vidéo à supprimer, seulement si elle est identique, octet
// par octet, à sa référence : la copie gardée, ou celle qui est transférée.
struct MediaDeletion {
    FileRef doomed;
    FileRef reference;

    friend bool operator==(const MediaDeletion&, const MediaDeletion&) = default;
};

// Un accompagnant qui part avec sa photo, seulement s'il est identique à son
// jumeau, du même genre, à côté de la copie qui reste.
struct CompanionDeletion {
    CompanionRef doomed;
    CompanionRef twin;

    friend bool operator==(const CompanionDeletion&, const CompanionDeletion&) = default;
};

// Ce que les réponses font des doublons. Les listes sont dans l'ordre des
// groupes. Les suppressions passent par la corbeille (E5).
struct DuplicatePlan {
    std::vector<std::size_t> not_transferred;  // rangs dans la source
    std::vector<std::size_t> transfer_renamed; // rangs dans la source
    std::vector<MediaDeletion> deletions;
    std::vector<CompanionDeletion> companion_deletions;
    // Accompagnants d'une photo supprimée qui n'ont pas de jumeau : ils
    // restent sur place et sont listés à la fin.
    std::vector<CompanionRef> companions_left;
};

// Rien tant qu'une question reste en suspens.
[[nodiscard]] std::optional<DuplicatePlan> plan_duplicates(const DuplicateSearch& search,
                                                           const Inventory& source_inventory,
                                                           const Inventory& destination_inventory,
                                                           const DuplicateAnswers& answers, TransferMode mode,
                                                           const DestinationDate& date_of);

} // namespace transfertphoto
