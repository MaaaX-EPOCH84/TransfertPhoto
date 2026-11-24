#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace transfertphoto {

// Les fichiers qui n'ont de sens qu'à côté d'une photo ou d'une vidéo, et
// qui la suivent dans son dossier de destination.
enum class CompanionKind {
    xmp,        // métadonnées XMP (Lightroom, darktable…)
    aae,        // retouches faites sur l'iPhone
    live_video, // vidéo d'une photo animée d'iPhone
    thm,        // aperçu d'une vidéo (GoPro, Canon)
    lrv,        // copie basse résolution d'une vidéo GoPro
    srt,        // télémétrie d'un drone DJI
};

// Genre d'accompagnant qu'annonce l'extension du dernier élément d'un chemin
// relatif, sans tenir compte de la casse. Le .mov n'en est pas : c'est d'abord
// une vidéo, qui ne devient accompagnant qu'à côté d'une photo de même nom.
[[nodiscard]] std::optional<CompanionKind> companion_kind_from_extension(std::string_view relative_path) noexcept;

struct CompanionLink {
    std::size_t companion; // indice dans la liste donnée
    std::size_t owner;     // indice de la photo ou de la vidéo qu'il suit
    CompanionKind kind;
};

struct CompanionAttachment {
    // Les vidéos de photos animées d'abord, puis les autres accompagnants dans
    // l'ordre de la liste.
    std::vector<CompanionLink> links;
    std::vector<std::size_t> orphans; // accompagnants sans photo ni vidéo, dans l'ordre
};

// Rattache chaque accompagnant de la liste à sa photo ou à sa vidéo, par le
// nom et dans le même dossier, la casse du nom ignorée (pas celle des dossiers,
// qui viennent tous du même parcours). Les chemins sont ceux d'une source :
// relatifs, UTF-8, séparés par « / ».
//
// - .xmp : au fichier dont il porte le nom entier (IMG_0001.CR3.xmp, à la
//   darktable), sinon au fichier de même nom sans extension (IMG_0001.xmp, à
//   la Lightroom), le RAW d'abord dans une paire RAW + JPEG.
// - .aae : à la photo de même nom, à défaut à la vidéo, ou à celle qu'il
//   désigne sous la forme IMG_O1234.AAE. La photo retouchée IMG_E1234 est une
//   photo à part entière.
// - .mov : à la photo de même nom quand il y en a une ; sinon c'est une vidéo.
// - .thm, .lrv, .srt : à la vidéo de même nom ; pour la GoPro, GL0x… désigne
//   aussi GH0x… et GX0x….
//
// Entre plusieurs candidats de même rang, le premier chemin dans l'ordre des
// octets l'emporte : le parcours d'une source ne garantit aucun ordre.
[[nodiscard]] CompanionAttachment attach_companions(std::span<const std::string_view> relative_paths);

} // namespace transfertphoto
