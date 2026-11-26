#pragma once

#include "core/file_hash.hpp"
#include "core/inventory.hpp"
#include "core/source.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <stop_token>
#include <string>
#include <system_error>
#include <vector>

namespace transfertphoto {

enum class Side {
    source,
    destination,
};

// Une photo ou une vidéo, par son côté et son rang dans Inventory::media.
struct FileRef {
    Side side = Side::source;
    std::size_t index = 0;

    friend bool operator==(const FileRef&, const FileRef&) = default;
};

// Des fichiers au contenu identique, deux au moins. Les membres de la source
// d'abord, puis ceux de la destination, chacun dans l'ordre des chemins.
struct DuplicateGroup {
    std::vector<FileRef> members;
    std::uint64_t size = 0;
    Hash128 hash;
};

// Un fichier écarté de la recherche faute d'avoir pu être lu en entier.
struct DuplicateError {
    Side side = Side::source;
    std::string relative_path;
    std::error_code error; // vide si le fichier a seulement changé
    bool changed = false;  // plus court que la taille relevée à l'inventaire
};

struct DuplicateSearch {
    ScanOutcome outcome = ScanOutcome::completed;
    // Dans l'ordre de leur premier membre. Vide si la recherche a été
    // interrompue ou si une source a disparu : elle est à refaire en entier.
    std::vector<DuplicateGroup> groups;
    std::vector<DuplicateError> errors;
};

struct DuplicateProgress {
    enum class Phase {
        heads,  // empreintes des débuts de fichiers
        wholes, // empreintes complètes de ceux dont les débuts concordent
    };
    Phase phase = Phase::heads;
    std::uint64_t bytes_done = 0;
    std::uint64_t bytes_total = 0;
};

// Ce qu'on lit du début de chaque candidat avant de décider de tout lire.
inline constexpr std::uint64_t duplicate_head_bytes = 64 * 1024;

// Cherche les photos et vidéos au contenu identique, dans la source, dans la
// destination et entre les deux : taille d'abord, puis empreinte des 64
// premiers Kio, puis empreinte complète. Seul ce qui partage une taille est
// lu, et seul ce qui partage aussi son début l'est en entier. Un fichier vide
// n'est le doublon de personne. on_progress est appelé à chaque bloc lu,
// depuis le fil appelant.
[[nodiscard]] DuplicateSearch find_duplicates(
    Source& source, const Inventory& source_inventory, Source& destination, const Inventory& destination_inventory,
    std::stop_token stop, const std::function<void(const DuplicateProgress&)>& on_progress = {});

} // namespace transfertphoto
