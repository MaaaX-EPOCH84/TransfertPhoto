#include "core/local_date_time.hpp"

#include <array>
#include <cstdio>
#include <ctime>

namespace transfertphoto {
namespace {

bool is_leap(int year) noexcept
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

unsigned days_in_month(int year, unsigned month) noexcept
{
    static constexpr std::array<unsigned, 12> days{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && is_leap(year))
        return 29;
    return days[month - 1];
}

bool is_digit(char c) noexcept
{
    return c >= '0' && c <= '9';
}

// Lit count chiffres à partir de position ; faux s'il en manque un.
bool read_number(std::string_view text, std::size_t position, std::size_t count, unsigned& value) noexcept
{
    if (position + count > text.size())
        return false;
    value = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const char c = text[position + i];
        if (!is_digit(c))
            return false;
        value = value * 10 + static_cast<unsigned>(c - '0');
    }
    return true;
}

} // namespace

bool is_valid(const LocalDateTime& value) noexcept
{
    if (value.month < 1 || value.month > 12)
        return false;
    if (value.day < 1 || value.day > days_in_month(value.year, value.month))
        return false;
    return value.hour <= 23 && value.minute <= 59 && value.second <= 60
           && value.nanosecond < 1'000'000'000;
}

std::string to_string(const LocalDateTime& value)
{
    std::array<char, 32> text{};
    std::snprintf(text.data(), text.size(), "%04d-%02u-%02u %02u:%02u:%02u", value.year, value.month,
                  value.day, value.hour, value.minute, value.second);
    return text.data();
}

LocalDateTime utc_from_unix_seconds(std::int64_t seconds) noexcept
{
    // Jours depuis 1970 et secondes dans le jour, arrondis vers le passé.
    std::int64_t days = seconds / 86'400;
    std::int64_t in_day = seconds % 86'400;
    if (in_day < 0) {
        in_day += 86'400;
        --days;
    }

    // Du nombre de jours à la date civile, par ères de 400 ans (algorithme
    // « civil_from_days » de Howard Hinnant).
    days += 719'468;
    const std::int64_t era = (days >= 0 ? days : days - 146'096) / 146'097;
    const auto day_of_era = static_cast<unsigned>(days - era * 146'097);
    const unsigned year_of_era = (day_of_era - day_of_era / 1460 + day_of_era / 36'524 - day_of_era / 146'096) / 365;
    const unsigned day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    const unsigned shifted_month = (5 * day_of_year + 2) / 153;

    LocalDateTime value;
    value.day = day_of_year - (153 * shifted_month + 2) / 5 + 1;
    value.month = shifted_month < 10 ? shifted_month + 3 : shifted_month - 9;
    value.year = static_cast<int>(static_cast<std::int64_t>(year_of_era) + era * 400 + (value.month <= 2 ? 1 : 0));
    value.hour = static_cast<unsigned>(in_day / 3600);
    value.minute = static_cast<unsigned>((in_day % 3600) / 60);
    value.second = static_cast<unsigned>(in_day % 60);
    return value;
}

std::int64_t unix_seconds_as_utc(const LocalDateTime& value) noexcept
{
    // « days_from_civil » de Howard Hinnant.
    const std::int64_t year = value.year - (value.month <= 2 ? 1 : 0);
    const std::int64_t era = (year >= 0 ? year : year - 399) / 400;
    const auto year_of_era = static_cast<unsigned>(year - era * 400);
    const unsigned day_of_year = (153 * (value.month > 2 ? value.month - 3 : value.month + 9) + 2) / 5 + value.day - 1;
    const unsigned day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    const std::int64_t days = era * 146'097 + static_cast<std::int64_t>(day_of_era) - 719'468;
    return days * 86'400 + value.hour * 3600 + value.minute * 60 + value.second;
}

LocalDateTime local_from_unix_seconds(std::int64_t seconds) noexcept
{
    const auto instant = static_cast<std::time_t>(seconds);
    std::tm local{};
#ifdef _WIN32
    if (::localtime_s(&local, &instant) != 0)
        return utc_from_unix_seconds(seconds);
#else
    if (::localtime_r(&instant, &local) == nullptr)
        return utc_from_unix_seconds(seconds);
#endif
    LocalDateTime value;
    value.year = local.tm_year + 1900;
    value.month = static_cast<unsigned>(local.tm_mon + 1);
    value.day = static_cast<unsigned>(local.tm_mday);
    value.hour = static_cast<unsigned>(local.tm_hour);
    value.minute = static_cast<unsigned>(local.tm_min);
    value.second = static_cast<unsigned>(local.tm_sec);
    return value;
}

std::optional<LocalDateTime> parse_exif_date_time(std::string_view text)
{
    // AAAA:MM:JJ HH:MM:SS, positions fixes.
    if (text.size() < 19)
        return std::nullopt;
    const char date_separator = text[4];
    if ((date_separator != ':' && date_separator != '-') || text[7] != date_separator)
        return std::nullopt;
    if (text[10] != ' ' && text[10] != 'T')
        return std::nullopt;
    if (text[13] != ':' || text[16] != ':')
        return std::nullopt;

    unsigned year = 0;
    LocalDateTime value;
    if (!read_number(text, 0, 4, year) || !read_number(text, 5, 2, value.month)
        || !read_number(text, 8, 2, value.day) || !read_number(text, 11, 2, value.hour)
        || !read_number(text, 14, 2, value.minute) || !read_number(text, 17, 2, value.second))
        return std::nullopt;
    value.year = static_cast<int>(year);

    // « 0000:00:00 00:00:00 » ne passe pas non plus : mois et jour à zéro.
    if (!is_valid(value))
        return std::nullopt;
    return value;
}

std::optional<std::uint32_t> parse_exif_subsecond(std::string_view text)
{
    // Certains appareils complètent par des blancs ou un zéro final.
    while (!text.empty() && (text.back() == ' ' || text.back() == '\0'))
        text.remove_suffix(1);
    if (text.empty())
        return std::nullopt;

    std::uint32_t value = 0;
    std::size_t digits = 0;
    for (const char c : text) {
        if (!is_digit(c))
            return std::nullopt;
        if (digits < 9) {
            value = value * 10 + static_cast<std::uint32_t>(c - '0');
            ++digits;
        }
    }
    for (; digits < 9; ++digits)
        value *= 10;
    return value;
}

} // namespace transfertphoto
