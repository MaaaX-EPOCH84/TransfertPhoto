#pragma once

#include "core/media_format.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>

namespace transfertphoto {

class Source;

// Octets à lire en tête de fichier pour reconnaître toutes les signatures :
// la plus exigeante est celle du flux MPEG-TS, dont je vérifie trois paquets
// de 192 octets.
inline constexpr std::size_t signature_head_size = 512;

// Format que révèlent les premiers octets, rien s'ils n'en révèlent aucun de
// la liste. Certains contenus ne se distinguent pas sur si peu d'octets : je
// rends alors le représentant de leur famille. Un en-tête TIFF ordinaire rend
// tiff (DNG, NEF et ARW en ont un), une vidéo ISO quelconque rend mp4 (MOV,
// M4V et 3GP en sont aussi), une vidéo QuickTime sans boîte ftyp rend mov.
[[nodiscard]] std::optional<MediaFormat> format_from_signature(std::span<const std::byte> head) noexcept;

// Vrai si un contenu reconnu comme detected peut légitimement porter
// l'extension de expected. Les RAW sur base TIFF acceptent un en-tête TIFF
// ordinaire, les vidéos ISO s'acceptent entre elles : je cherche un fichier
// qui n'est pas ce qu'il prétend être, pas la marque exacte de l'appareil.
[[nodiscard]] bool signature_matches(MediaFormat expected, std::optional<MediaFormat> detected) noexcept;

enum class SignatureVerdict {
    matches,    // la signature confirme l'extension
    mismatch,   // le contenu est autre chose, ou trop court pour être quoi que ce soit
    unreadable, // le début du fichier n'a pas pu être lu
};

struct SignatureCheck {
    SignatureVerdict verdict = SignatureVerdict::unreadable;
    // Ce que le contenu semble être ; rien s'il n'a pas été lu ou n'est pas reconnu.
    std::optional<MediaFormat> detected;
    std::error_code error; // seulement pour unreadable
};

[[nodiscard]] SignatureCheck check_signature(MediaFormat expected, std::span<const std::byte> head) noexcept;

// Lit le début du fichier dans la source, puis le juge comme ci-dessus.
[[nodiscard]] SignatureCheck check_signature(Source& source, std::string_view relative_path, MediaFormat expected);

} // namespace transfertphoto
