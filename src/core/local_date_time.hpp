#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace transfertphoto {

// Une date et une heure de calendrier, sans fuseau : celle que montrait
// l'horloge de l'appareil, ou une heure UTC déjà convertie à l'heure locale de
// cette machine. C'est elle qui donne le dossier AAAA/MM/JJ.
//
// Je n'emploie pas std::chrono::local_time : les bibliothèques standard que
// la CI emploie (GCC 11 sous Ubuntu 22.04, la libc++ d'Apple) ne fournissent
// pas toutes les fuseaux horaires, et il n'en faut aucun ici.
struct LocalDateTime {
    int year = 0;
    unsigned month = 0;  // 1 à 12
    unsigned day = 0;    // 1 à 31
    unsigned hour = 0;   // 0 à 23
    unsigned minute = 0; // 0 à 59
    unsigned second = 0; // 0 à 60, la seconde intercalaire existe
    // Fraction de seconde, pour ranger dans l'ordre les vues d'une rafale.
    std::uint32_t nanosecond = 0;

    friend auto operator<=>(const LocalDateTime&, const LocalDateTime&) = default;
};

// Vrai si la date existe au calendrier grégorien et si l'heure est bornée.
[[nodiscard]] bool is_valid(const LocalDateTime& value) noexcept;

// « AAAA-MM-JJ HH:MM:SS », pour le journal et les messages des tests.
[[nodiscard]] std::string to_string(const LocalDateTime& value);

// Lit une date EXIF, « AAAA:MM:JJ HH:MM:SS ». Tolère les tirets dans la date
// et un T entre la date et l'heure, que certains logiciels écrivent, et ce qui
// suit les secondes (fuseau, zéro final). Rien pour une date vide, à blancs ou
// à zéros, que les appareils écrivent quand ils n'en ont pas, ni pour une
// date qui n'existe pas.
[[nodiscard]] std::optional<LocalDateTime> parse_exif_date_time(std::string_view text);

// Lit une fraction de seconde EXIF (SubSecTime…) : des chiffres, dont seuls
// les neuf premiers comptent. Rien si ce n'en sont pas.
[[nodiscard]] std::optional<std::uint32_t> parse_exif_subsecond(std::string_view text);

} // namespace transfertphoto
