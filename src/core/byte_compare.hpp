#pragma once

#include "core/source.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <stop_token>
#include <string_view>
#include <system_error>

namespace transfertphoto {

enum class Comparison {
    identical,
    different,   // tailles ou contenus différents
    changed,     // un fichier n'a plus la taille relevée à l'inventaire
    unreadable,  // une lecture a échoué
    interrupted,
};

struct CompareResult {
    Comparison outcome = Comparison::identical;
    std::uint64_t first_difference = 0; // pour different : le premier octet qui diffère
    std::error_code error;              // pour unreadable
    bool first_failed = false;          // pour unreadable : le premier fichier, sinon le second
};

// Un fichier, sa source et la taille relevée à l'inventaire.
struct FileToCompare {
    Source& source;
    std::string_view relative_path;
    std::uint64_t size = 0;
};

inline constexpr std::size_t compare_block_size = 1024 * 1024;

// Compare deux fichiers octet par octet, lus en parallèle par blocs de 1 Mio :
// le préalable de toute suppression d'un doublon, qui n'a lieu que sur
// « identical ». Des tailles différentes concluent sans rien lire. Chaque
// fichier doit finir exactement à sa taille relevée : plus court ou plus
// long, il a changé. on_bytes reçoit les octets lus des deux côtés ; l'arrêt
// se vérifie entre deux blocs.
[[nodiscard]] CompareResult compare_files(const FileToCompare& first, const FileToCompare& second,
                                          std::stop_token stop,
                                          const std::function<void(std::uint64_t)>& on_bytes = {});

} // namespace transfertphoto
