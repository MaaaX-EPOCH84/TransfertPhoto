#pragma once

#include "core/file_hash.hpp"
#include "core/source.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>

namespace transfertphoto {

// Le suffixe du nom temporaire, et le nom tout entier : caché par son point,
// pour qu'un transfert interrompu ne laisse ni photo ni doublon apparent.
inline constexpr std::string_view temporary_suffix = ".transfertphoto";
[[nodiscard]] std::string temporary_name(std::string_view name);

struct CopyRequest {
    Source& source;
    std::string_view relative_path; // dans la source
    std::uint64_t size = 0;         // relevée à l'inventaire
    std::chrono::system_clock::time_point modified; // gardée sur la copie
    std::filesystem::path folder;   // dossier cible, créé au besoin
    std::string name;               // nom final, UTF-8
    bool verify = false;            // relire la copie et comparer son empreinte
};

struct CopyResult {
    enum class Outcome {
        copied,
        name_taken,    // un fichier a pris ce nom entre-temps : rien d'écrit, en choisir un autre
        read_failed,   // la source n'a pas pu être lue
        write_failed,  // la destination n'a pas pu être écrite
        changed,       // la source n'a plus la taille relevée à l'inventaire
        verify_failed, // la copie relue ne concorde pas ; elle a été effacée
        interrupted,
    };
    Outcome outcome = Outcome::copied;
    std::error_code error;
    Hash128 hash;               // de la source, calculée pendant la copie
    bool verified = false;      // la copie a été relue et concorde
    std::filesystem::path path; // de la copie, pour copied
};

// Copie un fichier d'une source vers la destination sans jamais rien écraser
// ni rien laisser d'inachevé : écriture sous un nom temporaire caché, données
// forcées sur le disque, date de modification de l'original, puis renommage
// qui échoue si le nom final existe. En cas d'échec ou d'arrêt, le fichier
// temporaire est effacé. on_bytes reçoit les octets lus de la source (et
// relus de la copie) ; l'arrêt se vérifie entre deux blocs.
[[nodiscard]] CopyResult copy_safely(const CopyRequest& request, std::stop_token stop,
                                     const std::function<void(std::uint64_t)>& on_bytes = {});

// Relit un fichier de la destination et compare son empreinte.
[[nodiscard]] bool matches_hash(const std::filesystem::path& file, std::uint64_t size, const Hash128& expected,
                                std::stop_token stop, std::error_code& error,
                                const std::function<void(std::uint64_t)>& on_bytes = {});

// Efface, sous root, les fichiers temporaires qu'un transfert précédent a pu
// laisser en s'arrêtant net. Rend leur nombre.
std::size_t remove_leftover_temporaries(const std::filesystem::path& root);

enum class RenameOutcome {
    renamed,
    target_exists,
    failed,
};

// Renomme from en to, sans jamais remplacer to.
[[nodiscard]] RenameOutcome rename_without_replacing(const std::filesystem::path& from,
                                                     const std::filesystem::path& to, std::error_code& error);

} // namespace transfertphoto
