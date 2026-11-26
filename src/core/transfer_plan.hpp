#pragma once

#include "core/dating.hpp"
#include "core/duplicate_decisions.hpp"
#include "core/duplicates.hpp"
#include "core/implausible_date.hpp"
#include "core/inventory.hpp"
#include "core/local_date_time.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace transfertphoto {

struct TransferOptions {
    TransferMode mode = TransferMode::copy;
    bool sort_by_date = true;    // la case « classer par date », cochée par défaut
    bool verify_copies = false;  // relire chaque copie ; toujours fait en déplacement
};

// Les choix faits devant les dates invraisemblables, au rang de
// Inventory::media de la source, et « appliquer à tous » pour ce transfert.
struct DateAnswers {
    std::vector<std::optional<ImplausibleDateChoice>> media; // peut être plus court
    std::optional<ImplausibleDateChoice> all;
};

// Une photo ou une vidéo à copier ou à déplacer, avec ses accompagnants.
struct FileOperation {
    std::size_t media_index = 0; // dans l'inventaire de la source
    std::string folder;          // relatif à la destination, « / » ; vide pour la racine
    std::string name;
    std::vector<std::string> companion_names; // au rang de MediaItem::companions
    LocalDateTime date;          // la date qui a décidé du dossier
    DateOrigin origin = DateOrigin::modification;
    std::uint64_t bytes = 0;     // la photo et ses accompagnants
};

// Ce que le récapitulatif montre avant de lancer : rien n'a encore été écrit.
struct TransferPlan {
    // Questions encore ouvertes : tant qu'il y en a, rien d'autre n'est rempli.
    std::vector<std::size_t> pending_dates; // rangs dans la source
    std::vector<PendingQuestion> pending_duplicates;

    std::vector<FileOperation> operations; // dans l'ordre des chemins de la source
    DuplicatePlan duplicates;              // le ménage, fait avant le transfert
    std::vector<std::size_t> skipped_for_date; // « ne pas transférer » devant une date

    std::uint64_t bytes_to_write = 0;
    std::optional<std::uint64_t> free_space; // de la destination, si on la connaît
    std::size_t companions = 0;              // accompagnants transférés

    // Rien à demander, et assez de place (ou une place inconnue).
    [[nodiscard]] bool ready() const noexcept;
    [[nodiscard]] bool enough_space() const noexcept;
};

// Rassemble tout ce qu'il faut pour lancer, sans rien écrire. destination_date
// sert à présélectionner la copie gardée parmi les doublons de la destination.
[[nodiscard]] TransferPlan plan_transfer(const Inventory& source, const Dating& source_dating,
                                         const std::vector<DateVerdict>& verdicts, const DateAnswers& date_answers,
                                         const Inventory& destination, const DuplicateSearch& duplicates,
                                         const DuplicateAnswers& duplicate_answers, const TransferOptions& options,
                                         const DestinationDate& destination_date,
                                         std::optional<std::uint64_t> free_space);

// L'espace libre du volume où se trouve path, ou de son plus proche parent qui
// existe (la destination n'existe pas toujours encore).
[[nodiscard]] std::optional<std::uint64_t> free_space_at(const std::filesystem::path& path);

} // namespace transfertphoto
