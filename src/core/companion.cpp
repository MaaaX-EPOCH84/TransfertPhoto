#include "core/companion.hpp"

#include "core/media_format.hpp"

#include <array>
#include <string>
#include <unordered_map>

namespace transfertphoto {

namespace {

using namespace std::string_view_literals;

struct CompanionExtension {
    std::string_view extension; // en minuscules, sans le point
    CompanionKind kind;
};

constexpr std::array companion_extensions{
    CompanionExtension{"xmp", CompanionKind::xmp},
    CompanionExtension{"aae", CompanionKind::aae},
    CompanionExtension{"thm", CompanionKind::thm},
    CompanionExtension{"lrv", CompanionKind::lrv},
    CompanionExtension{"srt", CompanionKind::srt},
};

constexpr char to_lower_ascii(char c) noexcept
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

std::string to_lower_ascii(std::string_view text)
{
    std::string result(text);
    for (char& c : result)
        c = to_lower_ascii(c);
    return result;
}

bool equals_ignoring_case(std::string_view a, std::string_view b) noexcept
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (to_lower_ascii(a[i]) != to_lower_ascii(b[i]))
            return false;
    }
    return true;
}

// Un chemin relatif découpé en dossier, nom, nom sans extension et extension.
struct PathParts {
    std::string_view folder; // vide à la racine
    std::string_view name;
    std::string_view stem;
    std::string_view extension; // vide s'il n'y en a pas
};

PathParts split(std::string_view relative_path) noexcept
{
    PathParts parts;
    const auto slash = relative_path.rfind('/');
    if (slash != std::string_view::npos) {
        parts.folder = relative_path.substr(0, slash);
        parts.name = relative_path.substr(slash + 1);
    } else {
        parts.name = relative_path;
    }
    // Même règle que format_from_extension() : « .xmp » seul n'a pas d'extension.
    const auto dot = parts.name.rfind('.');
    if (dot == std::string_view::npos || dot == 0) {
        parts.stem = parts.name;
    } else {
        parts.stem = parts.name.substr(0, dot);
        parts.extension = parts.name.substr(dot + 1);
    }
    return parts;
}

// Clé de recherche : le dossier tel quel, puis le nom replié en minuscules.
std::string key_of(std::string_view folder, std::string_view name)
{
    std::string key(folder);
    key += '/';
    key += to_lower_ascii(name);
    return key;
}

bool is_raw(MediaFormat format) noexcept
{
    switch (format) {
    case MediaFormat::dng:
    case MediaFormat::cr2:
    case MediaFormat::cr3:
    case MediaFormat::nef:
    case MediaFormat::arw:
    case MediaFormat::raf:
    case MediaFormat::orf:
    case MediaFormat::rw2:
        return true;
    default:
        return false;
    }
}

// Rang d'un candidat pour un genre d'accompagnant, le plus petit l'emportant ;
// rien si ce candidat ne peut pas le porter.
std::optional<int> rank(CompanionKind kind, MediaFormat owner) noexcept
{
    const bool photo = kind_of(owner) == MediaKind::photo;
    switch (kind) {
    case CompanionKind::xmp:
        // Lightroom écrit le .xmp du RAW, le JPEG d'une paire n'en a pas.
        if (!photo)
            return 2;
        return is_raw(owner) ? 0 : 1;
    case CompanionKind::aae:
        // L'iPhone écrit aussi un .aae pour une vidéo raccourcie.
        if (!photo)
            return 2;
        return is_raw(owner) ? 1 : 0;
    case CompanionKind::live_video:
        if (!photo)
            return std::nullopt;
        return is_raw(owner) ? 1 : 0;
    case CompanionKind::thm:
    case CompanionKind::lrv:
    case CompanionKind::srt:
        if (photo)
            return std::nullopt;
        return 0;
    }
    return std::nullopt;
}

struct Media {
    std::size_t index;
    MediaFormat format;
};

class Matcher {
public:
    explicit Matcher(std::span<const std::string_view> paths) : paths_(paths), withdrawn_(paths.size(), false) {}

    void add(std::size_t index, MediaFormat format, const PathParts& parts)
    {
        by_stem_[key_of(parts.folder, parts.stem)].push_back({index, format});
        by_name_[key_of(parts.folder, parts.name)].push_back({index, format});
    }

    // Le meilleur porteur de même nom sans extension, autre que except.
    [[nodiscard]] std::optional<std::size_t> by_stem(std::string_view folder, std::string_view stem,
                                                     CompanionKind kind,
                                                     std::size_t except = no_index) const
    {
        return best(by_stem_, key_of(folder, stem), kind, except);
    }

    [[nodiscard]] std::optional<std::size_t> by_name(std::string_view folder, std::string_view name,
                                                     CompanionKind kind) const
    {
        return best(by_name_, key_of(folder, name), kind, no_index);
    }

    // Un porteur devenu lui-même accompagnant (la vidéo d'une photo animée)
    // ne porte plus rien.
    void withdraw(std::size_t index) { withdrawn_[index] = true; }

    static constexpr std::size_t no_index = static_cast<std::size_t>(-1);

private:
    using Index = std::unordered_map<std::string, std::vector<Media>>;

    [[nodiscard]] std::optional<std::size_t> best(const Index& index, const std::string& key,
                                                  CompanionKind kind, std::size_t except) const
    {
        const auto found = index.find(key);
        if (found == index.end())
            return std::nullopt;
        std::optional<std::size_t> chosen;
        int chosen_rank = 0;
        for (const auto& candidate : found->second) {
            if (candidate.index == except || withdrawn_[candidate.index])
                continue;
            const auto candidate_rank = rank(kind, candidate.format);
            if (!candidate_rank)
                continue;
            if (!chosen || *candidate_rank < chosen_rank
                || (*candidate_rank == chosen_rank && paths_[candidate.index] < paths_[*chosen])) {
                chosen = candidate.index;
                chosen_rank = *candidate_rank;
            }
        }
        return chosen;
    }

    std::span<const std::string_view> paths_;
    Index by_stem_;
    Index by_name_;
    std::vector<bool> withdrawn_;
};

// IMG_O1234 → IMG_1234 : le .aae des retouches sous iOS récent. Rien si le
// nom n'a pas cette forme.
std::optional<std::string> aae_original_stem(std::string_view stem)
{
    const auto underscore = stem.rfind('_');
    if (underscore == std::string_view::npos || underscore + 2 >= stem.size())
        return std::nullopt;
    if (to_lower_ascii(stem[underscore + 1]) != 'o')
        return std::nullopt;
    const auto digits = stem.substr(underscore + 2);
    for (const char c : digits) {
        if (c < '0' || c > '9')
            return std::nullopt;
    }
    std::string original(stem.substr(0, underscore + 1));
    original += digits;
    return original;
}

// GL010123 → GH010123 et GX010123 : la GoPro nomme sa copie basse résolution
// d'une autre lettre que la vidéo.
std::array<std::string, 2> gopro_video_stems(std::string_view stem)
{
    const auto rest = stem.substr(2);
    return {std::string("GH").append(rest), std::string("GX").append(rest)};
}

} // namespace

std::optional<CompanionKind> companion_kind_from_extension(std::string_view relative_path) noexcept
{
    const auto parts = split(relative_path);
    for (const auto& entry : companion_extensions) {
        if (equals_ignoring_case(parts.extension, entry.extension))
            return entry.kind;
    }
    return std::nullopt;
}

CompanionAttachment attach_companions(std::span<const std::string_view> relative_paths)
{
    Matcher matcher(relative_paths);
    std::vector<std::size_t> movies;
    for (std::size_t i = 0; i < relative_paths.size(); ++i) {
        const auto format = format_from_extension(relative_paths[i]);
        if (!format)
            continue;
        matcher.add(i, *format, split(relative_paths[i]));
        if (*format == MediaFormat::mov)
            movies.push_back(i);
    }

    CompanionAttachment result;

    // Les photos animées d'abord : une vidéo qui en fait partie ne doit plus
    // être candidate pour les autres accompagnants.
    for (const auto movie : movies) {
        const auto parts = split(relative_paths[movie]);
        if (const auto photo = matcher.by_stem(parts.folder, parts.stem, CompanionKind::live_video, movie)) {
            result.links.push_back({movie, *photo, CompanionKind::live_video});
            matcher.withdraw(movie);
        }
    }

    for (std::size_t i = 0; i < relative_paths.size(); ++i) {
        const auto kind = companion_kind_from_extension(relative_paths[i]);
        if (!kind)
            continue;
        const auto parts = split(relative_paths[i]);

        std::optional<std::size_t> owner;
        switch (*kind) {
        case CompanionKind::xmp:
            owner = matcher.by_name(parts.folder, parts.stem, *kind);
            if (!owner)
                owner = matcher.by_stem(parts.folder, parts.stem, *kind);
            break;
        case CompanionKind::aae:
            owner = matcher.by_stem(parts.folder, parts.stem, *kind);
            if (!owner) {
                if (const auto original = aae_original_stem(parts.stem))
                    owner = matcher.by_stem(parts.folder, *original, *kind);
            }
            break;
        case CompanionKind::thm:
        case CompanionKind::lrv:
            owner = matcher.by_stem(parts.folder, parts.stem, *kind);
            if (!owner && parts.stem.size() > 2 && equals_ignoring_case(parts.stem.substr(0, 2), "gl"sv)) {
                for (const auto& stem : gopro_video_stems(parts.stem)) {
                    owner = matcher.by_stem(parts.folder, stem, *kind);
                    if (owner)
                        break;
                }
            }
            break;
        case CompanionKind::srt:
        case CompanionKind::live_video:
            owner = matcher.by_stem(parts.folder, parts.stem, *kind);
            break;
        }

        if (owner)
            result.links.push_back({i, *owner, *kind});
        else
            result.orphans.push_back(i);
    }
    return result;
}

} // namespace transfertphoto
