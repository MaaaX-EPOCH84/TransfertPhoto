#include "core/duplicates.hpp"

#include <algorithm>
#include <iterator>
#include <map>
#include <optional>
#include <tuple>
#include <utility>

namespace transfertphoto {
namespace {

struct Candidate {
    FileRef ref;
    const SourceEntry* entry = nullptr;
    Hash128 head;
    Hash128 whole;
};

DuplicateSearch abandoned(ScanOutcome outcome, std::vector<DuplicateError> errors)
{
    DuplicateSearch search;
    search.outcome = outcome;
    search.errors = std::move(errors);
    return search;
}

// Range des candidats par une clé, et garde les paquets de deux au moins.
template <typename Key>
std::vector<std::vector<Candidate>> bundles(std::vector<Candidate> candidates, Key key)
{
    std::ranges::stable_sort(candidates, {}, key);
    std::vector<std::vector<Candidate>> result;
    for (std::size_t begin = 0; begin < candidates.size();) {
        std::size_t end = begin + 1;
        while (end < candidates.size() && key(candidates[end]) == key(candidates[begin]))
            ++end;
        if (end - begin >= 2)
            result.emplace_back(candidates.begin() + static_cast<std::ptrdiff_t>(begin),
                                candidates.begin() + static_cast<std::ptrdiff_t>(end));
        begin = end;
    }
    return result;
}

} // namespace

DuplicateSearch find_duplicates(Source& source, const Inventory& source_inventory, Source& destination,
                                const Inventory& destination_inventory, std::stop_token stop,
                                const std::function<void(const DuplicateProgress&)>& on_progress)
{
    DuplicateSearch search;
    const auto source_of = [&](Side side) -> Source& { return side == Side::source ? source : destination; };

    // Tous les fichiers non vides, puis ceux qui partagent leur taille.
    std::vector<Candidate> all;
    for (const auto& [side, inventory] : {std::pair{Side::source, &source_inventory},
                                          std::pair{Side::destination, &destination_inventory}}) {
        for (std::size_t i = 0; i < inventory->media.size(); ++i) {
            const auto& entry = inventory->media[i].entry;
            if (entry.size > 0)
                all.push_back({{side, i}, &entry, {}, {}});
        }
    }
    std::vector<Candidate> same_size;
    for (auto& bundle : bundles(std::move(all), [](const Candidate& c) { return c.entry->size; }))
        std::ranges::move(bundle, std::back_inserter(same_size));

    // Empreinte de max_bytes octets ; faux si le fichier sort de la recherche,
    // ou s'il faut tout arrêter (outcome posé).
    DuplicateProgress progress;
    std::optional<ScanOutcome> stopped;
    const auto hash_of = [&](const Candidate& candidate, std::uint64_t max_bytes) -> std::optional<Hash128> {
        auto& side_source = source_of(candidate.ref.side);
        const auto result = hash_file(side_source, candidate.entry->relative_path, candidate.entry->size, stop,
                                      max_bytes, [&](std::uint64_t bytes) {
                                          progress.bytes_done += bytes;
                                          if (on_progress)
                                              on_progress(progress);
                                      });
        if (result.interrupted) {
            stopped = ScanOutcome::interrupted;
            return std::nullopt;
        }
        if (result.source_error && !side_source.is_available()) {
            stopped = ScanOutcome::source_unavailable;
            return std::nullopt;
        }
        if (!result.hash) {
            search.errors.push_back(
                {candidate.ref.side, candidate.entry->relative_path, result.source_error, result.changed});
            return std::nullopt;
        }
        return result.hash;
    };

    // Les débuts.
    progress.phase = DuplicateProgress::Phase::heads;
    for (const auto& candidate : same_size)
        progress.bytes_total += std::min(candidate.entry->size, duplicate_head_bytes);
    std::vector<Candidate> with_head;
    for (auto candidate : same_size) {
        if (stop.stop_requested())
            return abandoned(ScanOutcome::interrupted, std::move(search.errors));
        const auto head = hash_of(candidate, duplicate_head_bytes);
        if (stopped)
            return abandoned(*stopped, std::move(search.errors));
        if (!head)
            continue;
        candidate.head = *head;
        // Un fichier qui tient dans son début est déjà lu en entier.
        if (candidate.entry->size <= duplicate_head_bytes)
            candidate.whole = *head;
        with_head.push_back(candidate);
    }

    // Les fichiers entiers, seulement là où les débuts concordent.
    const auto by_head = [](const Candidate& c) { return std::tuple(c.entry->size, c.head); };
    auto head_bundles = bundles(std::move(with_head), by_head);
    progress = {DuplicateProgress::Phase::wholes, 0, 0};
    for (const auto& bundle : head_bundles) {
        if (bundle.front().entry->size > duplicate_head_bytes)
            progress.bytes_total += bundle.size() * bundle.front().entry->size;
    }
    std::vector<Candidate> with_whole;
    for (auto& bundle : head_bundles) {
        for (auto candidate : bundle) {
            if (stop.stop_requested())
                return abandoned(ScanOutcome::interrupted, std::move(search.errors));
            if (candidate.entry->size > duplicate_head_bytes) {
                const auto whole = hash_of(candidate, whole_file);
                if (stopped)
                    return abandoned(*stopped, std::move(search.errors));
                if (!whole)
                    continue;
                candidate.whole = *whole;
            }
            with_whole.push_back(candidate);
        }
    }

    const auto by_whole = [](const Candidate& c) { return std::tuple(c.entry->size, c.whole); };
    for (auto& bundle : bundles(std::move(with_whole), by_whole)) {
        DuplicateGroup group;
        group.size = bundle.front().entry->size;
        group.hash = bundle.front().whole;
        std::ranges::sort(bundle, [](const Candidate& a, const Candidate& b) {
            return std::tuple(a.ref.side, a.entry->relative_path) < std::tuple(b.ref.side, b.entry->relative_path);
        });
        for (const auto& candidate : bundle)
            group.members.push_back(candidate.ref);
        search.groups.push_back(std::move(group));
    }

    // Dans l'ordre du premier membre : côté, puis chemin.
    const auto path_of = [&](const FileRef& ref) -> const std::string& {
        const auto& inventory = ref.side == Side::source ? source_inventory : destination_inventory;
        return inventory.media[ref.index].entry.relative_path;
    };
    std::ranges::sort(search.groups, [&](const DuplicateGroup& a, const DuplicateGroup& b) {
        const auto& first_a = a.members.front();
        const auto& first_b = b.members.front();
        return std::tie(first_a.side, path_of(first_a)) < std::tie(first_b.side, path_of(first_b));
    });
    return search;
}

} // namespace transfertphoto
