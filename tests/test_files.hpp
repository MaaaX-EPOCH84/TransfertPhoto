#pragma once

// Fichiers et dossiers fabriqués pour les tests qui passent par le disque.

#include <atomic>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <string_view>
#include <system_error>

namespace test_support {

// Dossier temporaire propre à un test, effacé à la sortie.
class TempDir {
public:
    TempDir()
    {
        static std::atomic<unsigned> counter{0};
        std::random_device random;
        path_ = std::filesystem::temp_directory_path()
                / ("transfertphoto-" + std::to_string(random()) + "-" + std::to_string(counter++));
        std::filesystem::create_directories(path_);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    ~TempDir()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

inline std::filesystem::path utf8_path(std::string_view text)
{
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

// Écrit le fichier, en créant ses dossiers au besoin.
inline void write_file(const std::filesystem::path& path, std::string_view content)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file.write(content.data(), static_cast<std::streamsize>(content.size()));
}

} // namespace test_support
