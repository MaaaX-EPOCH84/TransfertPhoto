#include "core/media_format.hpp"

#include <array>
#include <cstddef>

namespace transfertphoto {

namespace {

constexpr std::array known_extensions{
    // Photos
    MediaExtension{"jpg", MediaFormat::jpeg},
    MediaExtension{"jpeg", MediaFormat::jpeg},
    MediaExtension{"png", MediaFormat::png},
    MediaExtension{"heic", MediaFormat::heif},
    MediaExtension{"heif", MediaFormat::heif},
    MediaExtension{"tif", MediaFormat::tiff},
    MediaExtension{"tiff", MediaFormat::tiff},
    MediaExtension{"webp", MediaFormat::webp},
    MediaExtension{"gif", MediaFormat::gif},
    MediaExtension{"dng", MediaFormat::dng},
    MediaExtension{"cr2", MediaFormat::cr2},
    MediaExtension{"cr3", MediaFormat::cr3},
    MediaExtension{"nef", MediaFormat::nef},
    MediaExtension{"arw", MediaFormat::arw},
    MediaExtension{"raf", MediaFormat::raf},
    MediaExtension{"orf", MediaFormat::orf},
    MediaExtension{"rw2", MediaFormat::rw2},
    // Vidéos
    MediaExtension{"mp4", MediaFormat::mp4},
    MediaExtension{"mov", MediaFormat::mov},
    MediaExtension{"m4v", MediaFormat::m4v},
    MediaExtension{"avi", MediaFormat::avi},
    MediaExtension{"mkv", MediaFormat::mkv},
    MediaExtension{"mts", MediaFormat::mpeg_ts},
    MediaExtension{"m2ts", MediaFormat::mpeg_ts},
    MediaExtension{"3gp", MediaFormat::three_gp},
};

// La plus longue extension de la table : au-delà, inutile de chercher.
constexpr std::size_t longest_extension = 4;

constexpr char to_lower_ascii(char c) noexcept
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

// La recherche compare une extension repliée en minuscules et bornée en
// longueur : une entrée de la table qui ne respecterait pas l'un ou l'autre ne
// serait jamais trouvée, sans que rien ne le signale.
constexpr bool table_is_well_formed()
{
    for (const auto& entry : known_extensions) {
        if (entry.extension.empty() || entry.extension.size() > longest_extension)
            return false;
        for (const char c : entry.extension) {
            if (to_lower_ascii(c) != c)
                return false;
        }
    }
    return true;
}
static_assert(table_is_well_formed());

} // namespace

std::span<const MediaExtension> media_extensions() noexcept
{
    return known_extensions;
}

MediaKind kind_of(MediaFormat format) noexcept
{
    switch (format) {
    case MediaFormat::jpeg:
    case MediaFormat::png:
    case MediaFormat::heif:
    case MediaFormat::tiff:
    case MediaFormat::webp:
    case MediaFormat::gif:
    case MediaFormat::dng:
    case MediaFormat::cr2:
    case MediaFormat::cr3:
    case MediaFormat::nef:
    case MediaFormat::arw:
    case MediaFormat::raf:
    case MediaFormat::orf:
    case MediaFormat::rw2:
        return MediaKind::photo;
    case MediaFormat::mp4:
    case MediaFormat::mov:
    case MediaFormat::m4v:
    case MediaFormat::avi:
    case MediaFormat::mkv:
    case MediaFormat::mpeg_ts:
    case MediaFormat::three_gp:
        return MediaKind::video;
    }
    // Valeur hors énumération : ne peut venir que d'un transtypage fautif.
    return MediaKind::photo;
}

std::optional<MediaFormat> format_from_extension(std::string_view relative_path) noexcept
{
    const auto slash = relative_path.rfind('/');
    const auto name = slash == std::string_view::npos ? relative_path : relative_path.substr(slash + 1);

    // Un nom qui commence par un point sans en avoir d'autre (« .jpg ») n'a
    // pas d'extension : c'est tout son nom.
    const auto dot = name.rfind('.');
    if (dot == std::string_view::npos || dot == 0)
        return std::nullopt;

    const auto extension = name.substr(dot + 1);
    if (extension.empty() || extension.size() > longest_extension)
        return std::nullopt;

    // Les extensions de la table sont en ASCII : replier la casse de l'ASCII
    // seul suffit, un octet UTF-8 non ASCII ne peut de toute façon rien égaler.
    std::array<char, longest_extension> buffer{};
    for (std::size_t i = 0; i < extension.size(); ++i)
        buffer[i] = to_lower_ascii(extension[i]);
    const std::string_view lowered(buffer.data(), extension.size());

    for (const auto& entry : known_extensions) {
        if (entry.extension == lowered)
            return entry.format;
    }
    return std::nullopt;
}

} // namespace transfertphoto
