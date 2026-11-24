#include "core/inventory.hpp"

#include "core/entry_filter.hpp"

#include <algorithm>
#include <optional>
#include <string_view>
#include <utility>

namespace transfertphoto {

namespace {

Inventory abandoned(ScanOutcome outcome, std::vector<SourceError> errors)
{
    Inventory inventory;
    inventory.outcome = outcome;
    inventory.errors = std::move(errors);
    return inventory;
}

} // namespace

std::size_t Inventory::anomaly_count() const noexcept
{
    const auto suspicious = std::ranges::count_if(media, [](const MediaItem& item) {
        return item.signature.verdict != SignatureVerdict::matches;
    });
    return static_cast<std::size_t>(suspicious) + errors.size();
}

Inventory take_inventory(Source& source, std::stop_token stop,
                         const std::function<void(const InventoryProgress&)>& on_progress)
{
    const auto report = [&](InventoryProgress::Phase phase, std::size_t done, std::size_t total) {
        if (on_progress)
            on_progress(InventoryProgress{phase, done, total});
    };

    std::vector<SourceEntry> entries;
    auto scan = source.scan(
        [&](const SourceEntry& entry) {
            entries.push_back(entry);
            report(InventoryProgress::Phase::scanning, entries.size(), 0);
        },
        stop);
    if (scan.outcome != ScanOutcome::completed)
        return abandoned(scan.outcome, std::move(scan.errors));

    // Le parcours ne promet aucun ordre ; l'inventaire, si.
    std::ranges::sort(entries, {}, &SourceEntry::relative_path);

    Inventory inventory;
    inventory.errors = std::move(scan.errors);

    // Photos, vidéos et accompagnants, à rapprocher par le nom ; leur rang
    // dans entries, et leur format pour les premières.
    std::vector<std::size_t> candidates;
    std::vector<std::optional<MediaFormat>> formats(entries.size());
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto classification = classify_entry(entries[i]);
        switch (classification.entry_class) {
        case EntryClass::excluded:
            inventory.excluded.push_back(entries[i]);
            break;
        case EntryClass::ignored:
            inventory.ignored.push_back(entries[i]);
            break;
        case EntryClass::media:
            formats[i] = classification.format;
            candidates.push_back(i);
            break;
        case EntryClass::companion:
            candidates.push_back(i);
            break;
        }
    }

    std::vector<std::string_view> paths;
    paths.reserve(candidates.size());
    for (const auto i : candidates)
        paths.emplace_back(entries[i].relative_path);
    const auto attachment = attach_companions(paths);

    // Ce qui suit une photo ou une vidéo n'en est plus une à part entière :
    // la vidéo d'une photo animée surtout.
    std::vector<std::vector<CompanionFile>> companions_of(entries.size());
    std::vector<bool> is_companion(entries.size(), false);
    for (const auto& link : attachment.links) {
        const auto companion = candidates[link.companion];
        companions_of[candidates[link.owner]].push_back({entries[companion], link.kind});
        is_companion[companion] = true;
    }
    for (const auto orphan : attachment.orphans)
        inventory.orphan_companions.push_back(entries[candidates[orphan]]);

    for (const auto i : candidates) {
        if (!formats[i] || is_companion[i])
            continue;
        MediaItem item;
        item.entry = entries[i];
        item.format = *formats[i];
        item.companions = std::move(companions_of[i]);
        std::ranges::sort(item.companions, {},
                          [](const CompanionFile& file) -> const std::string& { return file.entry.relative_path; });
        inventory.media.push_back(std::move(item));
    }

    const auto total = inventory.media.size();
    for (std::size_t i = 0; i < total; ++i) {
        if (stop.stop_requested())
            return abandoned(ScanOutcome::interrupted, std::move(inventory.errors));
        auto& item = inventory.media[i];
        item.signature = check_signature(source, item.entry.relative_path, item.format);
        // Un fichier illisible peut être le premier signe d'une source qui
        // s'en va : téléphone débranché, carte retirée.
        if (item.signature.verdict == SignatureVerdict::unreadable && !source.is_available())
            return abandoned(ScanOutcome::source_unavailable, std::move(inventory.errors));
        report(InventoryProgress::Phase::checking, i + 1, total);
    }
    return inventory;
}

} // namespace transfertphoto
