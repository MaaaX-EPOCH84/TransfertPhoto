#pragma once

#include "core/source.hpp"

#include <filesystem>

namespace transfertphoto {

// Une source qui est un dossier du système de fichiers : disque, clé USB,
// carte mémoire dans un lecteur.
//
// Les liens symboliques vers des dossiers ne sont pas suivis, pour ne pas
// tourner en rond ; ceux qui mènent à un fichier le sont.
class DirectorySource final : public Source {
public:
    explicit DirectorySource(std::filesystem::path root);

    [[nodiscard]] std::string display_name() const override;
    [[nodiscard]] bool is_available() const override;

    ScanResult scan(const std::function<void(const SourceEntry&)>& on_entry,
                    std::stop_token stop) override;

    std::size_t read_range(std::string_view relative_path,
                           std::uint64_t offset,
                           std::span<std::byte> buffer,
                           std::error_code& error) override;

    [[nodiscard]] std::optional<std::filesystem::path>
    local_path(std::string_view relative_path) const override;

private:
    std::filesystem::path root_;
};

} // namespace transfertphoto
