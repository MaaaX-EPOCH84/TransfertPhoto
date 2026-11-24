#pragma once

#include <optional>
#include <span>
#include <string_view>

namespace transfertphoto {

enum class MediaKind {
    photo,
    video,
};

// Un format par entrée de la liste arrêtée ; plusieurs extensions peuvent
// désigner le même (jpg et jpeg, tif et tiff, heic et heif, mts et m2ts).
enum class MediaFormat {
    jpeg,
    png,
    heif,
    tiff,
    webp,
    gif,
    dng,
    cr2,
    cr3,
    nef,
    arw,
    raf,
    orf,
    rw2,
    mp4,
    mov,
    m4v,
    avi,
    mkv,
    mpeg_ts,
    three_gp,
};

struct MediaExtension {
    std::string_view extension; // en minuscules, sans le point
    MediaFormat format;
};

// Toutes les extensions reconnues, chacune une seule fois.
[[nodiscard]] std::span<const MediaExtension> media_extensions() noexcept;

[[nodiscard]] MediaKind kind_of(MediaFormat format) noexcept;

// Format annoncé par l'extension du dernier élément d'un chemin relatif de
// source (UTF-8, séparateur « / »), sans tenir compte de la casse. Rien si
// l'extension n'est pas dans la liste. Ce n'est qu'une présomption : la
// signature des premiers octets la confirme ou non.
[[nodiscard]] std::optional<MediaFormat> format_from_extension(std::string_view relative_path) noexcept;

} // namespace transfertphoto
