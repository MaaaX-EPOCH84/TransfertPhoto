#include "core/entry_filter.hpp"

#include <array>

namespace transfertphoto {

namespace {

using namespace std::string_view_literals;

// Dossiers que Windows et Android posent à la racine d'une carte ou d'un
// disque. Sous Windows ils sont cachés ; ailleurs, seul leur nom les trahit.
constexpr std::array system_folders{
    "System Volume Information"sv,
    "$RECYCLE.BIN"sv,
    "RECYCLER"sv,
    "LOST.DIR"sv,
};

constexpr char to_lower_ascii(char c) noexcept
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
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

bool is_system_folder(std::string_view name) noexcept
{
    for (const auto folder : system_folders) {
        if (equals_ignoring_case(name, folder))
            return true;
    }
    return false;
}

} // namespace

bool is_excluded(const SourceEntry& entry) noexcept
{
    if (entry.hidden)
        return true;

    const std::string_view path = entry.relative_path;
    std::size_t start = 0;
    while (start <= path.size()) {
        const auto slash = path.find('/', start);
        const bool last = slash == std::string_view::npos;
        const auto component = path.substr(start, last ? std::string_view::npos : slash - start);
        if (!component.empty() && component.front() == '.')
            return true;
        // Un fichier qui porterait le nom d'un de ces dossiers n'a rien de
        // caché : seuls les dossiers comptent.
        if (!last && is_system_folder(component))
            return true;
        if (last)
            break;
        start = slash + 1;
    }
    return false;
}

EntryClassification classify_entry(const SourceEntry& entry) noexcept
{
    EntryClassification result;
    if (is_excluded(entry)) {
        result.entry_class = EntryClass::excluded;
        return result;
    }
    if (const auto format = format_from_extension(entry.relative_path)) {
        result.entry_class = EntryClass::media;
        result.format = format;
        return result;
    }
    if (const auto companion = companion_kind_from_extension(entry.relative_path)) {
        result.entry_class = EntryClass::companion;
        result.companion = companion;
        return result;
    }
    result.entry_class = EntryClass::ignored;
    return result;
}

} // namespace transfertphoto
