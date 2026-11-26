#pragma once

// Une source en mémoire pour les tests qui lisent le contenu des fichiers.

#include "core/source.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace test_support {

// Source en mémoire qui retient la plus grande lecture demandée. Elle peut
// aussi se dire locale, en désignant un fichier du disque dont le contenu
// n'est pas forcément le sien : on voit ainsi lequel des deux a été lu.
class MemorySource final : public transfertphoto::Source {
public:
    std::map<std::string, std::vector<std::uint8_t>> contents;
    std::map<std::string, std::filesystem::path> local_paths;
    std::error_code failure;          // rendue par toute lecture
    std::set<std::string> failing;    // chemins dont la lecture échoue
    bool available = true;
    bool vanishes_on_failure = false; // une lecture en échec emporte la source
    std::size_t largest_request = 0;
    std::map<std::string, int> reads; // lectures par chemin
    std::uint64_t bytes_read = 0;     // octets rendus, toutes lectures confondues

    std::string display_name() const override { return "mémoire"; }
    bool is_available() const override { return available; }
    transfertphoto::ScanResult scan(const std::function<void(const transfertphoto::SourceEntry&)>&,
                                    std::stop_token) override
    {
        return {};
    }

    std::size_t read_range(std::string_view relative_path, std::uint64_t offset,
                           std::span<std::byte> buffer, std::error_code& error) override
    {
        largest_request = std::max(largest_request, buffer.size());
        ++reads[std::string(relative_path)];
        error = failure;
        if (!error && (!available || failing.contains(std::string(relative_path))))
            error = std::make_error_code(std::errc::io_error);
        if (error) {
            if (vanishes_on_failure)
                available = false;
            return 0;
        }
        const auto& content = contents.at(std::string(relative_path));
        if (offset >= content.size())
            return 0;
        const auto start = static_cast<std::size_t>(offset);
        const auto count = std::min(buffer.size(), content.size() - start);
        for (std::size_t i = 0; i < count; ++i)
            buffer[i] = static_cast<std::byte>(content[start + i]);
        bytes_read += count;
        return count;
    }

    std::optional<std::filesystem::path> local_path(std::string_view relative_path) const override
    {
        const auto found = local_paths.find(std::string(relative_path));
        if (found == local_paths.end())
            return std::nullopt;
        return found->second;
    }
};

} // namespace test_support
