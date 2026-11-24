#include "core/entry_filter.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>

// Noms de tests sans accents : voir version_test.cpp.

using transfertphoto::classify_entry;
using transfertphoto::CompanionKind;
using transfertphoto::EntryClass;
using transfertphoto::is_excluded;
using transfertphoto::MediaFormat;
using transfertphoto::SourceEntry;

namespace {

SourceEntry entry(std::string path, bool hidden = false)
{
    SourceEntry result;
    result.relative_path = std::move(path);
    result.hidden = hidden;
    return result;
}

EntryClass class_of(std::string path, bool hidden = false)
{
    return classify_entry(entry(std::move(path), hidden)).entry_class;
}

} // namespace

TEST_CASE("une photo, une video et un accompagnant sont classes par l'extension")
{
    const auto photo = classify_entry(entry("DCIM/100CANON/IMG_0001.CR3"));
    CHECK(photo.entry_class == EntryClass::media);
    CHECK(photo.format == MediaFormat::cr3);
    CHECK_FALSE(photo.companion);

    CHECK(classify_entry(entry("DCIM/100APPLE/IMG_1234.MOV")).format == MediaFormat::mov);

    const auto sidecar = classify_entry(entry("DCIM/100CANON/IMG_0001.xmp"));
    CHECK(sidecar.entry_class == EntryClass::companion);
    CHECK(sidecar.companion == CompanionKind::xmp);
    CHECK_FALSE(sidecar.format);
}

TEST_CASE("tout autre fichier est ignore")
{
    CHECK(class_of("notes.txt") == EntryClass::ignored);
    CHECK(class_of("DCIM/100CANON/IMG_0001") == EntryClass::ignored);
    CHECK(class_of("MISC/AUTPRINT.MRK") == EntryClass::ignored);
    CHECK(class_of("Thumbs.db") == EntryClass::ignored);
    // Un fichier qui porte le nom d'un dossier système n'est pas caché pour autant.
    CHECK(class_of("DCIM/LOST.DIR") == EntryClass::ignored);
    CHECK_FALSE(classify_entry(entry("notes.txt")).format);
    CHECK_FALSE(classify_entry(entry("notes.txt")).companion);
}

TEST_CASE("un nom en point est exclu, fichier ou dossier")
{
    // Ce que macOS laisse sur une carte en exFAT : exclu, pas une photo.
    CHECK(class_of("DCIM/100CANON/._IMG_0001.JPG") == EntryClass::excluded);
    // La corbeille d'Android et ses vignettes.
    CHECK(class_of("DCIM/Camera/.trashed-1735689600-IMG_20241231.jpg") == EntryClass::excluded);
    CHECK(class_of("DCIM/.thumbnails/1700000000000.jpg") == EntryClass::excluded);
    // Dossiers que macOS pose à la racine d'une carte.
    CHECK(class_of(".Spotlight-V100/Store-V2/store.db") == EntryClass::excluded);
    CHECK(class_of(".Trashes/501/IMG_0002.JPG") == EntryClass::excluded);
    CHECK(class_of(".fseventsd/fseventsd-uuid") == EntryClass::excluded);
    // Un point ailleurs qu'en tête ne cache rien.
    CHECK(class_of("Vacances.2026/IMG_0003.JPG") == EntryClass::media);
}

TEST_CASE("l'attribut cache exclut le fichier")
{
    // DirectorySource le pose aussi quand c'est un dossier parent qui est caché.
    CHECK(class_of("DCIM/100CANON/IMG_0001.JPG", true) == EntryClass::excluded);
    CHECK(class_of("desktop.ini", true) == EntryClass::excluded);
    CHECK(class_of("DCIM/100CANON/IMG_0001.JPG", false) == EntryClass::media);
}

TEST_CASE("les dossiers systeme d'une carte sont exclus par leur nom")
{
    CHECK(class_of("System Volume Information/IndexerVolumeGuid") == EntryClass::excluded);
    CHECK(class_of("$RECYCLE.BIN/S-1-5-21-1000/$RABC123.jpg") == EntryClass::excluded);
    CHECK(class_of("$Recycle.Bin/S-1-5-21-1000/$IABC123.jpg") == EntryClass::excluded);
    CHECK(class_of("RECYCLER/INFO2") == EntryClass::excluded);
    CHECK(class_of("LOST.DIR/12345") == EntryClass::excluded);
    CHECK(class_of("lost.dir/12345.jpg") == EntryClass::excluded);
    // Plus bas dans l'arborescence aussi : une copie de carte dans un dossier.
    CHECK(class_of("Sauvegarde/Carte SD/LOST.DIR/12345.jpg") == EntryClass::excluded);
    // Un nom qui n'en est qu'un fragment ne compte pas.
    CHECK(class_of("LOST.DIR.old/IMG_0001.JPG") == EntryClass::media);
}

TEST_CASE("is_excluded et la classification s'accordent")
{
    for (const auto* path : {"a.jpg", ".a.jpg", "x/.y/a.jpg", "LOST.DIR/a.jpg", "a.txt", "a.xmp"}) {
        INFO(path);
        CHECK(is_excluded(entry(path)) == (class_of(path) == EntryClass::excluded));
    }
}
