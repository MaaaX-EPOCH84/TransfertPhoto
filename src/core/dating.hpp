#pragma once

#include "core/inventory.hpp"
#include "core/local_date_time.hpp"
#include "core/source.hpp"

#include <cstddef>
#include <functional>
#include <stop_token>
#include <vector>

namespace transfertphoto {

// D'où vient une date : la fenêtre le montre à côté.
enum class DateOrigin {
    metadata,     // EXIF de la photo, conteneur de la vidéo
    file_name,    // lue dans le nom du fichier
    modification, // date de modification du fichier, à l'heure locale
};

struct DateCandidate {
    LocalDateTime value;
    DateOrigin origin = DateOrigin::modification;
    bool has_time = true; // faux pour un nom qui ne donne que le jour
};

struct MediaDates {
    // Toutes les dates trouvées, dans l'ordre de repli : métadonnées, nom,
    // modification. Jamais vide, la date de modification existant toujours.
    // Les suivantes servent quand la première est invraisemblable.
    std::vector<DateCandidate> candidates;

    [[nodiscard]] const DateCandidate& retained() const { return candidates.front(); }
};

struct Dating {
    ScanOutcome outcome = ScanOutcome::completed;
    // Une entrée par photo ou vidéo de l'inventaire, au même rang. Ses
    // accompagnants, la vidéo d'une photo animée comprise, prennent sa date :
    // ils n'en ont pas à eux, et ne sont pas lus.
    // Vide si la datation a été interrompue ou si la source a disparu : elle
    // est à refaire en entier.
    std::vector<MediaDates> media;
    // Métadonnées que la source n'a pas pu lire, alors qu'elle était encore
    // là ; ces fichiers sont datés par leur nom ou leur modification.
    std::vector<SourceError> errors;
};

struct DatingProgress {
    std::size_t done = 0;
    std::size_t total = 0;
};

// Date chaque photo et vidéo de l'inventaire. Les métadonnées se lisent selon
// le format que révèle le contenu quand il contredit l'extension. on_progress
// est appelé après chaque fichier, depuis le fil appelant.
[[nodiscard]] Dating date_media(Source& source, const Inventory& inventory, std::stop_token stop,
                                const std::function<void(const DatingProgress&)>& on_progress = {});

} // namespace transfertphoto
