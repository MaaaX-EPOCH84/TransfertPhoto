#include "core/transfer_plan.hpp"

#include "core/destination_path.hpp"

#include <algorithm>
#include <set>
#include <system_error>

namespace transfertphoto {
namespace {

std::string file_name(const std::string& relative_path)
{
    const auto slash = relative_path.rfind('/');
    return slash == std::string::npos ? relative_path : relative_path.substr(slash + 1);
}

} // namespace

bool TransferPlan::enough_space() const noexcept
{
    return !free_space || *free_space >= bytes_to_write;
}

bool TransferPlan::ready() const noexcept
{
    return pending_dates.empty() && pending_duplicates.empty() && enough_space();
}

TransferPlan plan_transfer(const Inventory& source, const Dating& source_dating,
                           const std::vector<DateVerdict>& verdicts, const DateAnswers& date_answers,
                           const Inventory& destination, const DuplicateSearch& duplicates,
                           const DuplicateAnswers& duplicate_answers, const TransferOptions& options,
                           const DestinationDate& destination_date, std::optional<std::uint64_t> free_space)
{
    TransferPlan plan;
    plan.free_space = free_space;

    // Les questions d'abord : sans leurs réponses, il n'y a pas de plan.
    plan.pending_duplicates = pending_questions(duplicates, duplicate_answers, options.mode);
    std::vector<Placement> placements(source.media.size());
    for (std::size_t i = 0; i < source.media.size(); ++i) {
        const auto own = i < date_answers.media.size() ? date_answers.media[i] : std::nullopt;
        placements[i] = place(source_dating.media[i], verdicts[i], own ? own : date_answers.all, options.sort_by_date);
        if (placements[i].kind == Placement::Kind::pending)
            plan.pending_dates.push_back(i);
    }
    if (!plan.pending_dates.empty() || !plan.pending_duplicates.empty())
        return plan;

    plan.duplicates = *plan_duplicates(duplicates, source, destination, duplicate_answers, options.mode,
                                       destination_date);
    const std::set<std::size_t> not_transferred(plan.duplicates.not_transferred.begin(),
                                                plan.duplicates.not_transferred.end());

    NameAllocator names(existing_paths(destination));
    for (std::size_t i = 0; i < source.media.size(); ++i) {
        if (not_transferred.contains(i))
            continue;
        const auto& placement = placements[i];
        if (placement.kind == Placement::Kind::skip) {
            plan.skipped_for_date.push_back(i);
            continue;
        }
        const auto& item = source.media[i];
        std::vector<std::string> companion_names;
        FileOperation operation;
        operation.media_index = i;
        operation.folder = placement.folder;
        operation.date = placement.date;
        operation.origin = source_dating.media[i].retained().origin;
        operation.bytes = item.entry.size;
        for (const auto& companion : item.companions) {
            companion_names.push_back(file_name(companion.entry.relative_path));
            operation.bytes += companion.entry.size;
        }
        // La date suivante change l'origine : c'est elle qu'on montre.
        for (const auto& candidate : source_dating.media[i].candidates) {
            if (candidate.value == placement.date) {
                operation.origin = candidate.origin;
                break;
            }
        }
        auto allocated = names.allocate(operation.folder, file_name(item.entry.relative_path), companion_names);
        operation.name = std::move(allocated.name);
        operation.companion_names = std::move(allocated.companions);
        plan.bytes_to_write += operation.bytes;
        plan.companions += operation.companion_names.size();
        plan.operations.push_back(std::move(operation));
    }
    return plan;
}

std::optional<std::uint64_t> free_space_at(const std::filesystem::path& path)
{
    std::error_code error;
    auto existing = path;
    while (!existing.empty() && !std::filesystem::exists(existing, error)) {
        const auto parent = existing.parent_path();
        if (parent == existing)
            break;
        existing = parent;
    }
    const auto info = std::filesystem::space(existing, error);
    if (error)
        return std::nullopt;
    return info.available;
}

} // namespace transfertphoto
