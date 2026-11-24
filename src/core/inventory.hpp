#pragma once

#include "core/companion.hpp"
#include "core/media_format.hpp"
#include "core/media_signature.hpp"
#include "core/source.hpp"

#include <cstddef>
#include <functional>
#include <stop_token>
#include <vector>

namespace transfertphoto {

struct CompanionFile {
    SourceEntry entry;
    CompanionKind kind;
};

// Une photo ou une vidéo à transférer, avec ce qui la suit.
struct MediaItem {
    SourceEntry entry;
    MediaFormat format;          // présumé par l'extension
    SignatureCheck signature;    // ce qu'en disent les premiers octets
    std::vector<CompanionFile> companions; // dans l'ordre des chemins
};

struct Inventory {
    ScanOutcome outcome = ScanOutcome::completed;
    // Chaque liste est dans l'ordre des chemins. Un inventaire interrompu ou
    // dont la source a disparu les laisse vides : il est à refaire en entier.
    std::vector<MediaItem> media;
    std::vector<SourceEntry> orphan_companions; // comptés et signalés, jamais transférés
    std::vector<SourceEntry> excluded;          // cachés, comptés à part
    std::vector<SourceEntry> ignored;           // laissés sur place, comptés
    std::vector<SourceError> errors;            // éléments que le parcours n'a pas pu lire

    // Ce que la fenêtre doit signaler : les photos et vidéos dont la signature
    // contredit l'extension ou n'a pas pu être lue, plus les erreurs du parcours.
    [[nodiscard]] std::size_t anomaly_count() const noexcept;
};

struct InventoryProgress {
    enum class Phase {
        scanning, // parcours de la source : total inconnu, done = fichiers vus
        checking, // lecture des signatures : done sur total
    };
    Phase phase = Phase::scanning;
    std::size_t done = 0;
    std::size_t total = 0;
};

// Parcourt la source, classe chaque fichier, rattache les accompagnants et
// vérifie la signature de chaque photo et vidéo. on_progress est appelé à
// chaque fichier vu puis à chaque signature lue, depuis le fil appelant.
[[nodiscard]] Inventory take_inventory(Source& source, std::stop_token stop,
                                       const std::function<void(const InventoryProgress&)>& on_progress = {});

} // namespace transfertphoto
