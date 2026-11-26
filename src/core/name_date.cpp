#include "core/name_date.hpp"

#include <cstddef>

namespace transfertphoto {
namespace {

bool is_digit(char c) noexcept
{
    return c >= '0' && c <= '9';
}

std::size_t digit_run(std::string_view text, std::size_t position) noexcept
{
    std::size_t end = position;
    while (end < text.size() && is_digit(text[end]))
        ++end;
    return end - position;
}

unsigned number(std::string_view text, std::size_t position, std::size_t count) noexcept
{
    unsigned value = 0;
    for (std::size_t i = 0; i < count; ++i)
        value = value * 10 + static_cast<unsigned>(text[position + i] - '0');
    return value;
}

bool is_separator(char c) noexcept
{
    return c == '-' || c == '_' || c == '.';
}

bool set_date(LocalDateTime& value, unsigned year, unsigned month, unsigned day) noexcept
{
    if (static_cast<int>(year) < name_date_first_year || static_cast<int>(year) > name_date_last_year)
        return false;
    value = {};
    value.year = static_cast<int>(year);
    value.month = month;
    value.day = day;
    return is_valid(value);
}

bool set_time(LocalDateTime& value, unsigned hour, unsigned minute, unsigned second) noexcept
{
    LocalDateTime candidate = value;
    candidate.hour = hour;
    candidate.minute = minute;
    candidate.second = second;
    // Pas de seconde intercalaire dans un nom de fichier.
    if (second > 59 || !is_valid(candidate))
        return false;
    value = candidate;
    return true;
}

// Une heure qui suit la date, à partir de position : quatre caractères au
// plus, sans chiffre, puis HHMMSS (et ses millisecondes) ou HH.MM.SS.
bool read_time_after(std::string_view name, std::size_t position, LocalDateTime& value) noexcept
{
    std::size_t start = position;
    while (start < name.size() && !is_digit(name[start]) && start - position < 4)
        ++start;
    if (start == position || start >= name.size() || !is_digit(name[start]))
        return false;

    const std::size_t run = digit_run(name, start);
    if (run == 6 || run == 9) {
        if (!set_time(value, number(name, start, 2), number(name, start + 2, 2), number(name, start + 4, 2)))
            return false;
        if (run == 9)
            value.nanosecond = number(name, start + 6, 3) * 1'000'000;
        return true;
    }
    if (run == 2 && start + 8 <= name.size()) {
        const char separator = name[start + 2];
        if ((separator == '.' || separator == '-' || separator == ':' || separator == '_')
            && name[start + 5] == separator && digit_run(name, start + 3) == 2 && digit_run(name, start + 6) == 2)
            return set_time(value, number(name, start, 2), number(name, start + 3, 2), number(name, start + 6, 2));
    }
    return false;
}

// Une date qui commence au début d'une suite de chiffres, à position.
std::optional<NameDate> date_at(std::string_view name, std::size_t position)
{
    const std::size_t run = digit_run(name, position);
    NameDate result;

    // AAAAMMJJ, éventuellement collé à HHMMSS et à des millisecondes.
    if (run == 8 || run == 14 || run == 17) {
        if (!set_date(result.value, number(name, position, 4), number(name, position + 4, 2),
                      number(name, position + 6, 2)))
            return std::nullopt;
        if (run >= 14) {
            if (!set_time(result.value, number(name, position + 8, 2), number(name, position + 10, 2),
                          number(name, position + 12, 2)))
                return std::nullopt;
            if (run == 17)
                result.value.nanosecond = number(name, position + 14, 3) * 1'000'000;
            result.has_time = true;
        } else {
            result.has_time = read_time_after(name, position + 8, result.value);
        }
        return result;
    }

    // AAAA-MM-JJ, le même séparateur deux fois.
    if (run == 4 && position + 10 <= name.size()) {
        const char separator = name[position + 4];
        if (is_separator(separator) && name[position + 7] == separator && digit_run(name, position + 5) == 2
            && digit_run(name, position + 8) == 2) {
            if (!set_date(result.value, number(name, position, 4), number(name, position + 5, 2),
                          number(name, position + 8, 2)))
                return std::nullopt;
            result.has_time = read_time_after(name, position + 10, result.value);
            return result;
        }
    }
    return std::nullopt;
}

} // namespace

std::optional<NameDate> date_from_file_name(std::string_view relative_path)
{
    std::string_view name = relative_path;
    if (const auto slash = name.rfind('/'); slash != std::string_view::npos)
        name.remove_prefix(slash + 1);
    // L'extension peut porter des chiffres (mp4, 3gp) : elle ne compte pas.
    if (const auto dot = name.rfind('.'); dot != std::string_view::npos && dot > 0)
        name = name.substr(0, dot);

    for (std::size_t position = 0; position < name.size();) {
        if (!is_digit(name[position])) {
            ++position;
            continue;
        }
        if (auto date = date_at(name, position))
            return date;
        position += digit_run(name, position);
    }
    return std::nullopt;
}

} // namespace transfertphoto
