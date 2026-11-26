#include "core/file_hash.hpp"

#define XXH_INLINE_ALL
#include <xxhash.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <memory>
#include <vector>

namespace transfertphoto {
namespace {

struct StateDeleter {
    void operator()(XXH3_state_t* state) const noexcept { XXH3_freeState(state); }
};

Hash128 from_xxh(const XXH128_hash_t& hash) noexcept
{
    return {hash.high64, hash.low64};
}

} // namespace

std::string to_hex(const Hash128& hash)
{
    std::array<char, 33> text{};
    std::snprintf(text.data(), text.size(), "%016llx%016llx", static_cast<unsigned long long>(hash.high),
                  static_cast<unsigned long long>(hash.low));
    return text.data();
}

Hash128 hash_bytes(std::span<const std::byte> bytes) noexcept
{
    return from_xxh(XXH3_128bits(bytes.data(), bytes.size()));
}

FileHash hash_file(Source& source, std::string_view relative_path, std::uint64_t size, std::stop_token stop,
                   std::uint64_t max_bytes, const std::function<void(std::uint64_t)>& on_bytes)
{
    FileHash result;
    // L'état est aligné sur 64 octets : je le laisse allouer par xxHash.
    const std::unique_ptr<XXH3_state_t, StateDeleter> state(XXH3_createState());
    if (!state || XXH3_128bits_reset(state.get()) != XXH_OK) {
        result.source_error = std::make_error_code(std::errc::not_enough_memory);
        return result;
    }

    const std::uint64_t wanted = std::min(size, max_bytes);
    std::vector<std::byte> buffer(static_cast<std::size_t>(std::min<std::uint64_t>(wanted, hash_block_size)));
    for (std::uint64_t done = 0; done < wanted;) {
        if (stop.stop_requested()) {
            result.interrupted = true;
            return result;
        }
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(wanted - done, buffer.size()));
        const auto read = source.read_range(relative_path, done, std::span(buffer.data(), count),
                                            result.source_error);
        if (result.source_error)
            return result;
        if (read == 0) {
            result.changed = true;
            return result;
        }
        (void)XXH3_128bits_update(state.get(), buffer.data(), read);
        done += read;
        if (on_bytes)
            on_bytes(read);
    }
    result.hash = from_xxh(XXH3_128bits_digest(state.get()));
    return result;
}

} // namespace transfertphoto
