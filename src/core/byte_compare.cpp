#include "core/byte_compare.hpp"

#include <algorithm>
#include <span>
#include <vector>

namespace transfertphoto {
namespace {

CompareResult unreadable(std::error_code error, bool first)
{
    CompareResult result;
    result.outcome = Comparison::unreadable;
    result.error = error;
    result.first_failed = first;
    return result;
}

CompareResult with(Comparison outcome)
{
    CompareResult result;
    result.outcome = outcome;
    return result;
}

} // namespace

CompareResult compare_files(const FileToCompare& first, const FileToCompare& second, std::stop_token stop,
                            const std::function<void(std::uint64_t)>& on_bytes)
{
    if (first.size != second.size) {
        CompareResult result = with(Comparison::different);
        result.first_difference = std::min(first.size, second.size);
        return result;
    }

    const std::uint64_t size = first.size;
    const auto block = static_cast<std::size_t>(std::min<std::uint64_t>(size, compare_block_size));
    std::vector<std::byte> a(block);
    std::vector<std::byte> b(block);
    std::error_code error;

    for (std::uint64_t done = 0; done < size;) {
        if (stop.stop_requested())
            return with(Comparison::interrupted);
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(size - done, block));

        const auto read_a = first.source.read_range(first.relative_path, done, std::span(a.data(), count), error);
        if (error)
            return unreadable(error, true);
        const auto read_b = second.source.read_range(second.relative_path, done, std::span(b.data(), count), error);
        if (error)
            return unreadable(error, false);
        if (read_a != count || read_b != count)
            return with(Comparison::changed);
        if (on_bytes)
            on_bytes(2 * static_cast<std::uint64_t>(count));

        const auto mismatch = std::mismatch(a.begin(), a.begin() + static_cast<std::ptrdiff_t>(count), b.begin());
        if (mismatch.first != a.begin() + static_cast<std::ptrdiff_t>(count)) {
            CompareResult result = with(Comparison::different);
            result.first_difference = done + static_cast<std::uint64_t>(mismatch.first - a.begin());
            return result;
        }
        done += count;
    }

    // Rien après la taille relevée : un fichier qui a grandi depuis
    // l'inventaire n'est plus celui qu'on croit.
    std::byte probe{};
    for (const auto* file : {&first, &second}) {
        const auto extra = file->source.read_range(file->relative_path, size, std::span(&probe, 1), error);
        if (error)
            return unreadable(error, file == &first);
        if (extra != 0)
            return with(Comparison::changed);
    }
    return with(Comparison::identical);
}

} // namespace transfertphoto
