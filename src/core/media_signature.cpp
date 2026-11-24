#include "core/media_signature.hpp"

#include "core/source.hpp"

#include <array>

namespace transfertphoto {

namespace {

// Vrai si head contient bytes à partir de offset. Les signatures sont écrites
// en chaînes : les octets nuls y sont notés « \0 ».
bool has_at(std::span<const std::byte> head, std::size_t offset, std::string_view bytes) noexcept
{
    if (head.size() < offset || head.size() - offset < bytes.size())
        return false;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (head[offset + i] != static_cast<std::byte>(bytes[i]))
            return false;
    }
    return true;
}

using namespace std::string_view_literals;

// Marques ISO de la boîte ftyp propres à HEIF : images seules, séquences, et
// les marques génériques mif1 et msf1.
constexpr std::array heif_brands{
    "heic"sv, "heix"sv, "heim"sv, "heis"sv, "hevc"sv, "hevx"sv, "hevm"sv, "hevs"sv, "mif1"sv, "msf1"sv,
};

// Boîtes par lesquelles commencent les vieux fichiers QuickTime, écrits avant
// que la boîte ftyp n'existe.
constexpr std::array quicktime_atoms{
    "moov"sv, "mdat"sv, "wide"sv, "free"sv, "skip"sv, "pnot"sv,
};

std::optional<MediaFormat> format_from_iso_box(std::span<const std::byte> head) noexcept
{
    if (has_at(head, 4, "ftyp")) {
        // Une boîte ftyp sans marque majeure n'est rien de reconnaissable.
        if (head.size() < 12)
            return std::nullopt;
        if (has_at(head, 8, "crx "))
            return MediaFormat::cr3;
        for (const auto brand : heif_brands) {
            if (has_at(head, 8, brand))
                return MediaFormat::heif;
        }
        return MediaFormat::mp4;
    }
    for (const auto atom : quicktime_atoms) {
        if (has_at(head, 4, atom))
            return MediaFormat::mov;
    }
    return std::nullopt;
}

// Un seul octet 0x47 (un « G ») ne prouve rien : j'exige qu'il revienne au
// début de trois paquets de suite. Les caméras AVCHD écrivent des paquets de
// 192 octets, un horodatage de 4 octets précédant chaque paquet de 188.
bool looks_like_mpeg_ts(std::span<const std::byte> head) noexcept
{
    constexpr auto sync = std::byte{0x47};
    constexpr int packets = 3;
    const auto repeats = [&](std::size_t first, std::size_t packet_size) {
        for (int i = 0; i < packets; ++i) {
            const auto offset = first + static_cast<std::size_t>(i) * packet_size;
            if (offset >= head.size() || head[offset] != sync)
                return false;
        }
        return true;
    };
    return repeats(0, 188) || repeats(4, 192);
}

} // namespace

std::optional<MediaFormat> format_from_signature(std::span<const std::byte> head) noexcept
{
    if (has_at(head, 0, "\xFF\xD8\xFF"))
        return MediaFormat::jpeg;
    if (has_at(head, 0, "\x89PNG\r\n\x1A\n"))
        return MediaFormat::png;
    if (has_at(head, 0, "GIF87a") || has_at(head, 0, "GIF89a"))
        return MediaFormat::gif;
    if (has_at(head, 0, "RIFF")) {
        if (has_at(head, 8, "WEBP"))
            return MediaFormat::webp;
        if (has_at(head, 8, "AVI "))
            return MediaFormat::avi;
        return std::nullopt;
    }
    if (has_at(head, 0, "\x1A\x45\xDF\xA3"))
        return MediaFormat::mkv;
    if (has_at(head, 0, "FUJIFILMCCD-RAW"))
        return MediaFormat::raf;

    // Olympus et Panasonic remplacent le nombre magique 42 du TIFF par le leur.
    if (has_at(head, 0, "IIRO") || has_at(head, 0, "IIRS") || has_at(head, 0, "MMOR"))
        return MediaFormat::orf;
    if (has_at(head, 0, "IIU\0"sv))
        return MediaFormat::rw2;
    if (has_at(head, 0, "II*\0"sv)) {
        // Canon signe le CR2 par « CR » juste après l'en-tête TIFF.
        if (has_at(head, 8, "CR"))
            return MediaFormat::cr2;
        return MediaFormat::tiff;
    }
    // Le second couple est celui du BigTIFF, au-delà de 4 Gio.
    if (has_at(head, 0, "MM\0*"sv) || has_at(head, 0, "II+\0"sv) || has_at(head, 0, "MM\0+"sv))
        return MediaFormat::tiff;

    if (const auto iso = format_from_iso_box(head))
        return iso;
    if (looks_like_mpeg_ts(head))
        return MediaFormat::mpeg_ts;
    return std::nullopt;
}

bool signature_matches(MediaFormat expected, std::optional<MediaFormat> detected) noexcept
{
    if (!detected)
        return false;
    const auto found = *detected;
    switch (expected) {
    case MediaFormat::tiff:
        // Un CR2 est un TIFF valide.
        return found == MediaFormat::tiff || found == MediaFormat::cr2;
    case MediaFormat::dng:
    case MediaFormat::nef:
    case MediaFormat::arw:
        return found == MediaFormat::tiff;
    case MediaFormat::orf:
    case MediaFormat::rw2:
        // Je ne suis pas certain que tous les boîtiers écrivent leur nombre
        // magique propre : un en-tête TIFF ordinaire passe aussi.
        return found == expected || found == MediaFormat::tiff;
    case MediaFormat::mp4:
    case MediaFormat::mov:
    case MediaFormat::m4v:
    case MediaFormat::three_gp:
        return found == MediaFormat::mp4 || found == MediaFormat::mov;
    case MediaFormat::jpeg:
    case MediaFormat::png:
    case MediaFormat::heif:
    case MediaFormat::webp:
    case MediaFormat::gif:
    case MediaFormat::cr2:
    case MediaFormat::cr3:
    case MediaFormat::raf:
    case MediaFormat::avi:
    case MediaFormat::mkv:
    case MediaFormat::mpeg_ts:
        return found == expected;
    }
    return false;
}

SignatureCheck check_signature(MediaFormat expected, std::span<const std::byte> head) noexcept
{
    SignatureCheck check;
    check.detected = format_from_signature(head);
    check.verdict = signature_matches(expected, check.detected) ? SignatureVerdict::matches
                                                                : SignatureVerdict::mismatch;
    return check;
}

SignatureCheck check_signature(Source& source, std::string_view relative_path, MediaFormat expected)
{
    std::array<std::byte, signature_head_size> buffer{};
    std::error_code error;
    const auto read = source.read_head(relative_path, buffer, error);
    if (error) {
        SignatureCheck check;
        check.verdict = SignatureVerdict::unreadable;
        check.error = error;
        return check;
    }
    return check_signature(expected, std::span<const std::byte>(buffer.data(), read));
}

} // namespace transfertphoto
