#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace transfertphoto {

// Un fichier tel que le voit une source. Le chemin est relatif à la racine de
// la source, en UTF-8, avec des « / » : une source n'est pas forcément un
// système de fichiers (un téléphone en MTP n'en est pas un), donc je ne promets
// pas un std::filesystem::path.
struct SourceEntry {
    std::string relative_path;
    std::uint64_t size = 0;
    std::chrono::system_clock::time_point modified;
    // Attribut « caché » du système, porté par le fichier ou par l'un de ses
    // dossiers sous la racine. Les noms qui commencent par un point ne sont pas
    // traités ici : c'est l'inventaire qui en décide.
    bool hidden = false;
};

// Ce qui n'a pas pu être lu pendant un parcours, qui continue malgré tout.
struct SourceError {
    std::string relative_path; // vide pour la racine
    std::error_code error;
};

enum class ScanOutcome {
    completed,
    interrupted,        // arrêt demandé par le std::stop_token
    source_unavailable, // source absente au départ, ou disparue en cours de route
};

struct ScanResult {
    ScanOutcome outcome = ScanOutcome::completed;
    std::vector<SourceError> errors;
};

// Ce que l'inventaire et le transfert attendent d'une source : un dossier, une
// carte, plus tard un téléphone ou un appareil photo.
class Source {
public:
    Source() = default;
    Source(const Source&) = delete;
    Source& operator=(const Source&) = delete;
    virtual ~Source() = default;

    // Nom à montrer dans la fenêtre et dans le journal.
    [[nodiscard]] virtual std::string display_name() const = 0;

    // Faux pour une source absente : téléphone débranché, carte retirée. Elle
    // peut revenir ; attendre son retour est l'affaire de l'appelant.
    [[nodiscard]] virtual bool is_available() const = 0;

    // Parcourt toute la source et appelle on_entry pour chaque fichier, dans un
    // ordre quelconque. Un élément illisible est noté dans le résultat sans
    // arrêter le parcours ; une source qui disparaît l'arrête.
    virtual ScanResult scan(const std::function<void(const SourceEntry&)>& on_entry,
                            std::stop_token stop) = 0;

    // Lit le début d'un fichier dans buffer et rend le nombre d'octets lus,
    // moins que la taille du tampon si le fichier est plus court.
    virtual std::size_t read_head(std::string_view relative_path,
                                  std::span<std::byte> buffer,
                                  std::error_code& error) = 0;
};

} // namespace transfertphoto
