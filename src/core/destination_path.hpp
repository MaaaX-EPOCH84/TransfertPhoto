#pragma once

#include "core/dating.hpp"
#include "core/implausible_date.hpp"
#include "core/inventory.hpp"
#include "core/local_date_time.hpp"

#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace transfertphoto {

// Où va un fichier dans la destination, avant de lui choisir un nom.
struct Placement {
    enum class Kind {
        folder,  // dans folder, relatif à la destination, « / » comme séparateur
        skip,    // ne pas transférer (choisi devant une date invraisemblable)
        pending, // date invraisemblable sans choix valable : la fenêtre doit demander
    };
    Kind kind = Kind::folder;
    std::string folder;      // vide : à la racine de la destination
    LocalDateTime date;      // la date qui a décidé du dossier
};

// AAAA/MM/JJ.
[[nodiscard]] std::string date_folder(const LocalDateTime& date);

// Le dossier d'une photo ou d'une vidéo : celui de sa date retenue, classement
// coché, ou la racine, décoché. Devant une date invraisemblable, le choix fait
// (ou appliqué à tous) décide : la date suivante vraisemblable, la date gardée,
// __A_TRIER__ (avec AAAA/MM/JJ de la date, même fausse, ou à plat), ou rien.
[[nodiscard]] Placement place(const MediaDates& dates, const DateVerdict& verdict,
                              std::optional<ImplausibleDateChoice> choice, bool sort_by_date);

// Les noms choisis pour une photo ou une vidéo et ses accompagnants.
struct AllocatedNames {
    std::string name;
    std::vector<std::string> companions; // dans l'ordre donné
};

// Donne à chaque fichier un nom libre dans son dossier, sans jamais reprendre
// celui d'un fichier déjà présent ou déjà attribué dans ce transfert :
// « nom.ext », sinon « nom (2).ext », « nom (3).ext »… Une photo et ses
// accompagnants prennent le même numéro, le premier qui les laisse tous libres,
// pour que les accompagnants se rattachent encore à elle par le nom.
//
// La comparaison des noms ignore la casse ASCII : sur les systèmes de fichiers
// qui l'ignorent (NTFS, APFS par défaut), « IMG.JPG » et « img.jpg » sont un
// seul fichier.
class NameAllocator {
public:
    // Les chemins déjà présents dans la destination, relatifs, « / ».
    explicit NameAllocator(const std::vector<std::string>& existing = {});

    [[nodiscard]] AllocatedNames allocate(std::string_view folder, std::string_view name,
                                          const std::vector<std::string>& companion_names);

    [[nodiscard]] bool is_taken(std::string_view folder, std::string_view name) const;

private:
    std::set<std::string> taken_; // chemins repliés en minuscules ASCII
};

// Tous les chemins d'un inventaire de destination : photos, vidéos, leurs
// accompagnants, orphelins, fichiers ignorés et exclus.
[[nodiscard]] std::vector<std::string> existing_paths(const Inventory& destination);

// Le nom d'un accompagnant quand sa photo change de nom : le début commun est
// remplacé (IMG_0001.HEIC.xmp → IMG_0001 (2).HEIC.xmp) ; sinon le même suffixe
// s'insère avant le premier point (IMG_O0001.AAE → IMG_O0001 (2).AAE).
[[nodiscard]] std::string renamed_companion(std::string_view companion, std::string_view old_carrier,
                                            std::string_view new_carrier, unsigned number);

} // namespace transfertphoto
