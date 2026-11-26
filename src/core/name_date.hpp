#pragma once

#include "core/local_date_time.hpp"

#include <optional>
#include <string_view>

namespace transfertphoto {

struct NameDate {
    LocalDateTime value; // à minuit quand le nom ne donne que le jour
    bool has_time = false;
};

// Bornes des années reconnues dans un nom. Large exprès : un scan nommé
// « 1985-07-14 Mariage.jpg » doit garder sa date ; c'est le contrôle des dates
// invraisemblables qui juge ensuite une date future ou d'une origine connue.
inline constexpr int name_date_first_year = 1900;
inline constexpr int name_date_last_year = 2099;

// Cherche une date dans le nom du fichier, dernier élément du chemin, sans
// son extension. Reconnaît :
//
//   AAAAMMJJ, seul ou suivi d'une heure : IMG_20231225_180400.jpg (Android),
//     PXL_20231225_180400123.jpg (Pixel, millisecondes), IMG-20231225-WA0001
//     (WhatsApp, le jour seul), IMG20231225180400.jpg (date et heure collées) ;
//   AAAA-MM-JJ, avec des tirets, des points ou des soulignés, suivi ou non
//     d'une heure HH.MM.SS, HH-MM-SS, HH:MM:SS ou HHMMSS : « 2023-12-25
//     18.04.00.jpg » (Dropbox), Screenshot_2023-12-25-18-04-00.png, « Capture
//     d'écran 2023-12-25 à 18.04.00.png » (macOS).
//
// Les chiffres d'une date ne touchent pas d'autres chiffres : 120231225 n'en
// est pas une. Entre la date et l'heure, quatre caractères au plus, aucun
// chiffre (« _ », « - », « T », « at », « à »). La première date valide
// l'emporte ; une date qui n'existe pas au calendrier est ignorée.
[[nodiscard]] std::optional<NameDate> date_from_file_name(std::string_view relative_path);

} // namespace transfertphoto
