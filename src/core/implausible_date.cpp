#include "core/implausible_date.hpp"

#include <algorithm>
#include <array>
#include <cstdint>

namespace transfertphoto {
namespace {

constexpr std::array<int, 4> origin_years{1904, 1970, 1980, 2000};

bool is_known_origin(const LocalDateTime& value) noexcept
{
    for (const int year : origin_years) {
        if (value.year == year && value.month == 1 && value.day == 1)
            return true;
        if (value.year == year - 1 && value.month == 12 && value.day == 31 && value.hour >= 12)
            return true;
    }
    return false;
}

// Les critères qui se jugent date par date.
unsigned own_reasons(const LocalDateTime& value, std::int64_t now_seconds) noexcept
{
    unsigned reasons = 0;
    if (value.year == 0)
        reasons |= null_date;
    if (is_known_origin(value))
        reasons |= known_origin;
    if (unix_seconds_as_utc(value) - now_seconds > 86'400)
        reasons |= future;
    return reasons;
}

} // namespace

ImplausibleDateChoice DateVerdict::preselected() const noexcept
{
    return replacement ? ImplausibleDateChoice::use_next : ImplausibleDateChoice::to_sort;
}

std::vector<ImplausibleDateChoice> DateVerdict::choices() const
{
    std::vector<ImplausibleDateChoice> result;
    if (replacement)
        result.push_back(ImplausibleDateChoice::use_next);
    result.insert(result.end(),
                  {ImplausibleDateChoice::keep, ImplausibleDateChoice::to_sort, ImplausibleDateChoice::skip});
    return result;
}

std::vector<DateVerdict> judge_dates(const Dating& dating, const LocalDateTime& now)
{
    const std::int64_t now_seconds = unix_seconds_as_utc(now);
    std::vector<DateVerdict> verdicts(dating.media.size());

    // Les critères propres à chaque date, et ce qui reste pour juger du lot.
    struct Dated {
        std::int64_t seconds;
        std::size_t index;
    };
    std::vector<Dated> remaining;
    for (std::size_t i = 0; i < dating.media.size(); ++i) {
        const auto& retained = dating.media[i].retained().value;
        verdicts[i].reasons = own_reasons(retained, now_seconds);
        if (verdicts[i].plausible())
            remaining.push_back({unix_seconds_as_utc(retained), i});
    }

    // Le plus ancien groupe : tout ce qui précède le premier écart de deux ans.
    std::optional<std::int64_t> rest_begins;
    std::ranges::sort(remaining, {}, &Dated::seconds);
    for (std::size_t i = 1; i < remaining.size(); ++i) {
        if (remaining[i].seconds - remaining[i - 1].seconds >= early_group_gap_seconds) {
            if (i < remaining.size() - i) {
                rest_begins = remaining[i].seconds;
                for (std::size_t j = 0; j < i; ++j)
                    verdicts[remaining[j].index].reasons |= early_group;
            }
            break;
        }
    }

    // La première date suivante qui échappe à tous les critères.
    for (std::size_t i = 0; i < dating.media.size(); ++i) {
        auto& verdict = verdicts[i];
        if (verdict.plausible())
            continue;
        const auto& candidates = dating.media[i].candidates;
        for (std::size_t c = 1; c < candidates.size(); ++c) {
            const auto& value = candidates[c].value;
            if (own_reasons(value, now_seconds) != 0)
                continue;
            if (rest_begins && unix_seconds_as_utc(value) < *rest_begins)
                continue;
            verdict.replacement = c;
            break;
        }
    }
    return verdicts;
}

} // namespace transfertphoto
