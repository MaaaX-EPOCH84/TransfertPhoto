#include "core/duplicate_situations.hpp"

#include <algorithm>

namespace transfertphoto {

GroupSituations situations_of(const DuplicateGroup& group) noexcept
{
    GroupSituations situations;
    for (const auto& member : group.members) {
        if (member.side == Side::source)
            ++situations.source_copies;
        else
            ++situations.destination_copies;
    }
    return situations;
}

std::vector<DuplicateSituation> GroupSituations::questions() const
{
    std::vector<DuplicateSituation> result;
    if (source_copies >= 1 && destination_copies >= 1)
        result.push_back(DuplicateSituation::between);
    else if (source_copies >= 2)
        result.push_back(DuplicateSituation::within_source);
    if (destination_copies >= 2)
        result.push_back(DuplicateSituation::within_destination);
    return result;
}

bool GroupSituations::has(DuplicateSituation situation) const
{
    const auto all = questions();
    return std::ranges::find(all, situation) != all.end();
}

} // namespace transfertphoto
