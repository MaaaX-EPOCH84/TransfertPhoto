#pragma once

#include "core/source.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>

namespace transfertphoto {

// Empreinte XXH3 sur 128 bits. Deux contenus différents qui la partagent sont
// assez improbables pour la tenir comme preuve d'identité, sauf avant une
// suppression : là, on compare octet par octet.
struct Hash128 {
    std::uint64_t high = 0;
    std::uint64_t low = 0;

    friend auto operator<=>(const Hash128&, const Hash128&) = default;
};

// Comme xxhsum -H2 : 32 chiffres hexadécimaux, poids fort d'abord.
[[nodiscard]] std::string to_hex(const Hash128& hash);

[[nodiscard]] Hash128 hash_bytes(std::span<const std::byte> bytes) noexcept;

inline constexpr std::size_t hash_block_size = 1024 * 1024;
inline constexpr std::uint64_t whole_file = std::numeric_limits<std::uint64_t>::max();

struct FileHash {
    std::optional<Hash128> hash; // absente si une lecture a échoué ou s'il a fallu s'arrêter
    std::error_code source_error;
    bool interrupted = false;
    // Le fichier s'est arrêté avant la taille annoncée : il a changé depuis
    // le parcours, et son empreinte ne dirait rien de sûr.
    bool changed = false;
};

// Empreinte des max_bytes premiers octets d'un fichier, ou de tout le
// fichier, lu par blocs de 1 Mio. size : la taille relevée par le parcours.
// on_bytes reçoit le nombre d'octets de chaque bloc lu, pour la progression ;
// l'arrêt se vérifie entre deux blocs.
[[nodiscard]] FileHash hash_file(Source& source, std::string_view relative_path, std::uint64_t size,
                                 std::stop_token stop, std::uint64_t max_bytes = whole_file,
                                 const std::function<void(std::uint64_t)>& on_bytes = {});

} // namespace transfertphoto
