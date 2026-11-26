#pragma once

#include "core/local_date_time.hpp"
#include "core/source.hpp"

#include <cstdint>
#include <optional>
#include <string_view>
#include <system_error>

namespace transfertphoto {

struct PhotoDateRead {
    // Date de prise de vue des métadonnées, à l'heure de l'appareil.
    std::optional<LocalDateTime> date;
    // Une lecture de la source a échoué : fichier disparu, téléphone débranché.
    // Une date absente ou des métadonnées illisibles n'en sont pas une.
    std::error_code source_error;
};

// Lit la date de prise de vue d'une photo par Exiv2 : DateTimeOriginal, à
// défaut DateTimeDigitized, avec la fraction de seconde qui accompagne celle
// qui a été retenue. DateTime, celui de l'IFD0, est ignoré : les logiciels y
// mettent la date de leur retouche.
//
// size : la taille du fichier relevée par le parcours. Ne lève jamais : un
// fichier que rien ne date, ou dont les métadonnées sont abîmées, n'a pas de
// date, et l'appelant passe au nom du fichier.
[[nodiscard]] PhotoDateRead read_photo_date(Source& source, std::string_view relative_path,
                                            std::uint64_t size);

} // namespace transfertphoto
