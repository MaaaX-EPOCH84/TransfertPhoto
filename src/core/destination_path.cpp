#include "core/destination_path.hpp"

#include <algorithm>
#include <array>
#include <cstdio>

namespace transfertphoto {
namespace {

char lower(char c) noexcept
{
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

std::string folded(std::string_view text)
{
    std::string result(text);
    for (auto& c : result)
        c = lower(c);
    return result;
}

bool starts_with_folded(std::string_view text, std::string_view prefix) noexcept
{
    if (text.size() < prefix.size())
        return false;
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (lower(text[i]) != lower(prefix[i]))
            return false;
    }
    return true;
}

std::string_view stem(std::string_view name) noexcept
{
    const auto dot = name.rfind('.');
    return dot == std::string_view::npos || dot == 0 ? name : name.substr(0, dot);
}

std::string suffix(unsigned number)
{
    return " (" + std::to_string(number) + ")";
}

// « nom.ext », ou « nom (n).ext » à partir de 2.
std::string numbered(std::string_view name, unsigned number)
{
    if (number <= 1)
        return std::string(name);
    const auto base = stem(name);
    return std::string(base) + suffix(number) + std::string(name.substr(base.size()));
}

std::string joined(std::string_view folder, std::string_view name)
{
    return folder.empty() ? std::string(name) : std::string(folder) + "/" + std::string(name);
}

} // namespace

std::string date_folder(const LocalDateTime& date)
{
    std::array<char, 24> text{};
    std::snprintf(text.data(), text.size(), "%04d/%02u/%02u", date.year, date.month, date.day);
    return text.data();
}

Placement place(const MediaDates& dates, const DateVerdict& verdict, std::optional<ImplausibleDateChoice> choice,
                bool sort_by_date)
{
    const auto in_date_folder = [&](const LocalDateTime& date, std::string_view prefix = {}) {
        Placement placement;
        placement.date = date;
        std::string folder(prefix);
        if (sort_by_date)
            folder = folder.empty() ? date_folder(date) : folder + "/" + date_folder(date);
        placement.folder = folder;
        return placement;
    };

    const auto& retained = dates.retained().value;
    if (verdict.plausible())
        return in_date_folder(retained);

    Placement pending;
    pending.kind = Placement::Kind::pending;
    pending.date = retained;
    if (!choice)
        return pending;

    switch (*choice) {
    case ImplausibleDateChoice::use_next:
        if (!verdict.replacement || *verdict.replacement >= dates.candidates.size())
            return pending;
        return in_date_folder(dates.candidates[*verdict.replacement].value);
    case ImplausibleDateChoice::keep:
        return in_date_folder(retained);
    case ImplausibleDateChoice::to_sort:
        return in_date_folder(retained, to_sort_folder);
    case ImplausibleDateChoice::skip: {
        Placement skipped;
        skipped.kind = Placement::Kind::skip;
        skipped.date = retained;
        return skipped;
    }
    }
    return pending;
}

std::string renamed_companion(std::string_view companion, std::string_view old_carrier, std::string_view new_carrier,
                              unsigned number)
{
    if (number <= 1)
        return std::string(companion);
    const auto old_stem = stem(old_carrier);
    // Le nom de la photo, suivi d'un point : IMG_0001.xmp, IMG_0001.HEIC.xmp.
    if (starts_with_folded(companion, old_stem) && companion.size() > old_stem.size()
        && companion[old_stem.size()] == '.')
        return std::string(stem(new_carrier)) + std::string(companion.substr(old_stem.size()));
    const auto dot = companion.find('.');
    if (dot == std::string_view::npos || dot == 0)
        return std::string(companion) + suffix(number);
    return std::string(companion.substr(0, dot)) + suffix(number) + std::string(companion.substr(dot));
}

NameAllocator::NameAllocator(const std::vector<std::string>& existing)
{
    for (const auto& path : existing)
        taken_.insert(folded(path));
}

bool NameAllocator::is_taken(std::string_view folder, std::string_view name) const
{
    return taken_.contains(folded(joined(folder, name)));
}

AllocatedNames NameAllocator::allocate(std::string_view folder, std::string_view name,
                                       const std::vector<std::string>& companion_names)
{
    for (unsigned number = 1;; ++number) {
        AllocatedNames names;
        names.name = numbered(name, number);
        std::set<std::string> wanted{folded(joined(folder, names.name))};
        for (const auto& companion : companion_names) {
            names.companions.push_back(renamed_companion(companion, name, names.name, number));
            wanted.insert(folded(joined(folder, names.companions.back())));
        }
        const bool free = std::ranges::none_of(wanted, [&](const std::string& path) { return taken_.contains(path); });
        if (free) {
            taken_.insert(wanted.begin(), wanted.end());
            return names;
        }
    }
}

std::vector<std::string> existing_paths(const Inventory& destination)
{
    std::vector<std::string> paths;
    for (const auto& item : destination.media) {
        paths.push_back(item.entry.relative_path);
        for (const auto& companion : item.companions)
            paths.push_back(companion.entry.relative_path);
    }
    for (const auto* list : {&destination.orphan_companions, &destination.excluded, &destination.ignored}) {
        for (const auto& entry : *list)
            paths.push_back(entry.relative_path);
    }
    return paths;
}

} // namespace transfertphoto
