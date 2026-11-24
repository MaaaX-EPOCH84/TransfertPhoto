#pragma once

#include "core/companion.hpp"
#include "core/media_format.hpp"
#include "core/source.hpp"

#include <optional>
#include <string_view>

namespace transfertphoto {

// Ce que l'inventaire fait d'un fichier de la source.
enum class EntryClass {
    media,     // photo ou vidéo de la liste, présumée par l'extension
    companion, // accompagnant présumé, à rattacher à une photo ou une vidéo
    excluded,  // caché : jamais transféré, compté à part
    ignored,   // tout le reste : laissé sur place, compté
};

struct EntryClassification {
    EntryClass entry_class = EntryClass::ignored;
    std::optional<MediaFormat> format;      // pour media
    std::optional<CompanionKind> companion; // pour companion
};

// Vrai pour un fichier caché ou rangé dans un dossier caché : nom qui commence
// par un point (._IMG_0001.JPG de macOS, .trashed-… de la corbeille Android,
// .thumbnails), attribut caché de Windows, ou dossier système d'une carte ou
// d'un disque (System Volume Information, $RECYCLE.BIN, RECYCLER, LOST.DIR),
// reconnu par son nom parce que l'attribut caché ne se voit que sous Windows.
[[nodiscard]] bool is_excluded(const SourceEntry& entry) noexcept;

// L'exclusion passe avant tout : un ._IMG_0001.JPG n'est pas une photo.
[[nodiscard]] EntryClassification classify_entry(const SourceEntry& entry) noexcept;

} // namespace transfertphoto
