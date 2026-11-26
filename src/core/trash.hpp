#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace transfertphoto {

enum class TrashOutcome {
    trashed,     // dans la corbeille du système
    unavailable, // volume sans corbeille : rien n'a été fait, la suppression
                 // définitive demande la double confirmation de la fenêtre
    failed,
};

struct TrashResult {
    TrashOutcome outcome = TrashOutcome::failed;
    std::error_code error;
};

// Vrai si un fichier de ce volume peut aller à la corbeille. Un volume
// amovible (carte, clé) ou réseau n'en a pas, selon ce qu'en dit le système :
// le fichier y resterait sur la carte, ce qu'on n'attend pas en la vidant.
[[nodiscard]] bool volume_has_trash(const std::filesystem::path& file);

// Met un fichier à la corbeille du système :
//
//   Windows : IFileOperation avec FOFX_RECYCLEONDELETE ;
//   macOS : NSFileManager trashItemAtURL ;
//   Linux : la spécification freedesktop.org de la corbeille, écrite ici, sans
//     GIO : celle de l'utilisateur ($XDG_DATA_HOME/Trash) pour un fichier de
//     son disque, sinon celle du disque du fichier ($topdir/.Trash/$uid ou
//     $topdir/.Trash-$uid).
//
// Sur un volume sans corbeille, ne touche à rien et rend unavailable.
[[nodiscard]] TrashResult move_to_trash(const std::filesystem::path& file);

// La suppression définitive, une fois la double confirmation donnée.
[[nodiscard]] std::error_code delete_permanently(const std::filesystem::path& file);

namespace trash_detail {

// Exposés pour les essais.

// Les systèmes de fichiers en réseau, d'après leur type dans /proc/self/mountinfo.
[[nodiscard]] bool is_network_filesystem(std::string_view type) noexcept;

// Le chemin d'un fichier tel que l'écrit une entrée .trashinfo : chaque octet
// hors des caractères non réservés de la RFC 2396 et de « / » s'écrit %XX.
[[nodiscard]] std::string encode_trash_path(std::string_view utf8_path);

} // namespace trash_detail

} // namespace transfertphoto
