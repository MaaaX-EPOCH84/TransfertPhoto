#include "core/duplicate_decisions.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <string_view>

namespace transfertphoto {
namespace {

template <typename Choice>
bool offered(const std::vector<Choice>& choices, Choice choice)
{
    return std::ranges::find(choices, choice) != choices.end();
}

// La réponse qui vaut pour un groupe : la sienne, sinon « appliquer à tous »,
// et seulement si elle est offerte dans ce mode.
template <typename Choice>
std::optional<Choice> resolved(const std::optional<Choice>& own, const std::optional<Choice>& all,
                               const std::vector<Choice>& choices)
{
    const auto answer = own ? own : all;
    if (answer && offered(choices, *answer))
        return answer;
    return std::nullopt;
}

struct Resolved {
    std::optional<BetweenChoice> between;
    std::optional<WithinSourceChoice> within_source;
    std::optional<WithinDestinationChoice> within_destination;
};

Resolved resolve(const DuplicateAnswers& answers, std::size_t group, TransferMode mode)
{
    const GroupAnswers none;
    const auto& own = group < answers.groups.size() ? answers.groups[group] : none;
    return {resolved(own.between, answers.all_between, between_choices(mode)),
            resolved(own.within_source, answers.all_within_source, within_source_choices(mode)),
            resolved(own.within_destination, answers.all_within_destination, within_destination_choices())};
}

const MediaItem& item_of(const FileRef& ref, const Inventory& source, const Inventory& destination)
{
    return (ref.side == Side::source ? source : destination).media[ref.index];
}

bool member_of(const DuplicateGroup& group, const std::optional<FileRef>& ref, Side side)
{
    return ref && ref->side == side && std::ranges::find(group.members, *ref) != group.members.end();
}

// Le dossier du fichier se termine-t-il par AAAA/MM/JJ de cette date ?
bool in_folder_of(std::string_view path, const LocalDateTime& date)
{
    std::array<char, 16> folder{};
    std::snprintf(folder.data(), folder.size(), "%04d/%02u/%02u/", date.year, date.month, date.day);
    const auto slash = path.rfind('/');
    if (slash == std::string_view::npos)
        return false;
    const std::string_view directory = path.substr(0, slash + 1);
    const std::string_view wanted(folder.data());
    if (directory.size() < wanted.size() || directory.substr(directory.size() - wanted.size()) != wanted)
        return false;
    // Le dossier de l'année doit être un élément entier du chemin.
    return directory.size() == wanted.size() || directory[directory.size() - wanted.size() - 1] == '/';
}

// Supprime doomed après comparaison avec reference, et règle le sort de ses
// accompagnants : partir avec lui s'ils ont un jumeau du même genre à côté
// d'une copie qui reste (reference d'abord), sinon rester et être listés.
void delete_with_companions(DuplicatePlan& plan, const FileRef& doomed, const FileRef& reference,
                            const std::vector<FileRef>& survivors, const Inventory& source,
                            const Inventory& destination)
{
    plan.deletions.push_back({doomed, reference});
    const auto& companions = item_of(doomed, source, destination).companions;
    for (std::size_t c = 0; c < companions.size(); ++c) {
        std::optional<CompanionRef> twin;
        for (const auto& survivor : survivors) {
            const auto& candidates = item_of(survivor, source, destination).companions;
            for (std::size_t t = 0; t < candidates.size() && !twin; ++t) {
                if (candidates[t].kind == companions[c].kind)
                    twin = CompanionRef{survivor, t};
            }
            if (twin)
                break;
        }
        if (twin)
            plan.companion_deletions.push_back({{doomed, c}, *twin});
        else
            plan.companions_left.push_back({doomed, c});
    }
}

} // namespace

std::vector<BetweenChoice> between_choices(TransferMode mode)
{
    if (mode == TransferMode::move)
        return {BetweenChoice::skip, BetweenChoice::skip_and_delete_source, BetweenChoice::transfer_renamed};
    return {BetweenChoice::skip, BetweenChoice::transfer_renamed};
}

std::vector<WithinSourceChoice> within_source_choices(TransferMode mode)
{
    if (mode == TransferMode::move)
        return {WithinSourceChoice::transfer_one, WithinSourceChoice::transfer_one_delete_others,
                WithinSourceChoice::transfer_all};
    return {WithinSourceChoice::transfer_one, WithinSourceChoice::transfer_all};
}

std::vector<WithinDestinationChoice> within_destination_choices()
{
    return {WithinDestinationChoice::keep_one, WithinDestinationChoice::leave};
}

std::vector<PendingQuestion> pending_questions(const DuplicateSearch& search, const DuplicateAnswers& answers,
                                               TransferMode mode)
{
    std::vector<PendingQuestion> pending;
    for (std::size_t g = 0; g < search.groups.size(); ++g) {
        const auto answer = resolve(answers, g, mode);
        for (const auto situation : situations_of(search.groups[g]).questions()) {
            const bool answered = (situation == DuplicateSituation::between && answer.between)
                                  || (situation == DuplicateSituation::within_source && answer.within_source)
                                  || (situation == DuplicateSituation::within_destination
                                      && answer.within_destination);
            if (!answered)
                pending.push_back({g, situation});
        }
    }
    return pending;
}

FileRef preselected_destination_copy(const DuplicateGroup& group, const Inventory& destination_inventory,
                                     const DestinationDate& date_of)
{
    std::optional<FileRef> first;
    for (const auto& member : group.members) {
        if (member.side != Side::destination)
            continue;
        if (!first)
            first = member;
        const auto date = date_of ? date_of(member.index) : std::nullopt;
        if (date && in_folder_of(destination_inventory.media[member.index].entry.relative_path, *date))
            return member;
    }
    return first.value_or(group.members.front());
}

FileRef preselected_source_copy(const DuplicateGroup& group)
{
    for (const auto& member : group.members) {
        if (member.side == Side::source)
            return member;
    }
    return group.members.front();
}

std::optional<DuplicatePlan> plan_duplicates(const DuplicateSearch& search, const Inventory& source_inventory,
                                             const Inventory& destination_inventory,
                                             const DuplicateAnswers& answers, TransferMode mode,
                                             const DestinationDate& date_of)
{
    if (!pending_questions(search, answers, mode).empty())
        return std::nullopt;

    DuplicatePlan plan;
    const GroupAnswers none;
    for (std::size_t g = 0; g < search.groups.size(); ++g) {
        const auto& group = search.groups[g];
        const auto& own = g < answers.groups.size() ? answers.groups[g] : none;
        const auto answer = resolve(answers, g, mode);
        const auto situations = situations_of(group);

        std::vector<FileRef> sources;
        std::vector<FileRef> destinations;
        for (const auto& member : group.members)
            (member.side == Side::source ? sources : destinations).push_back(member);

        // La copie qui reste en destination, s'il y en a plusieurs.
        const FileRef kept_destination = destinations.empty() ? FileRef{}
                                   : member_of(group, own.destination_kept, Side::destination)
                                       ? *own.destination_kept
                                       : preselected_destination_copy(group, destination_inventory, date_of);
        if (situations.has(DuplicateSituation::within_destination)
            && answer.within_destination == WithinDestinationChoice::keep_one) {
            for (const auto& copy : destinations) {
                if (copy != kept_destination)
                    delete_with_companions(plan, copy, kept_destination, {kept_destination}, source_inventory,
                                           destination_inventory);
            }
        }

        if (situations.has(DuplicateSituation::between)) {
            // Les copies de la destination qui restent : la gardée, ou toutes.
            std::vector<FileRef> remaining{kept_destination};
            if (answer.within_destination != WithinDestinationChoice::keep_one) {
                for (const auto& copy : destinations) {
                    if (copy != kept_destination)
                        remaining.push_back(copy);
                }
            }
            for (const auto& copy : sources) {
                switch (*answer.between) {
                case BetweenChoice::skip:
                    plan.not_transferred.push_back(copy.index);
                    break;
                case BetweenChoice::skip_and_delete_source:
                    plan.not_transferred.push_back(copy.index);
                    delete_with_companions(plan, copy, kept_destination, remaining, source_inventory,
                                           destination_inventory);
                    break;
                case BetweenChoice::transfer_renamed:
                    plan.transfer_renamed.push_back(copy.index);
                    break;
                }
            }
        } else if (situations.has(DuplicateSituation::within_source)
                   && *answer.within_source != WithinSourceChoice::transfer_all) {
            const FileRef kept =
                member_of(group, own.source_kept, Side::source) ? *own.source_kept : preselected_source_copy(group);
            for (const auto& copy : sources) {
                if (copy == kept)
                    continue;
                plan.not_transferred.push_back(copy.index);
                if (*answer.within_source == WithinSourceChoice::transfer_one_delete_others)
                    delete_with_companions(plan, copy, kept, {kept}, source_inventory, destination_inventory);
            }
        }
    }
    return plan;
}

} // namespace transfertphoto
