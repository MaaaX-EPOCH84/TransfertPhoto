#include "core/directory_source.hpp"

#include <fstream>
#include <ios>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace transfertphoto {
namespace {

std::string to_utf8(const fs::path& path)
{
    const std::u8string text = path.generic_u8string();
    return {text.begin(), text.end()};
}

fs::path from_utf8(std::string_view text)
{
    return fs::path(std::u8string(text.begin(), text.end()));
}

std::chrono::system_clock::time_point to_system_time(fs::file_time_type time)
{
#if defined(__cpp_lib_chrono) && __cpp_lib_chrono >= 201907L
    return std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        std::chrono::clock_cast<std::chrono::system_clock>(time));
#else
    // Sans clock_cast, je recale par rapport à l'instant présent. L'écart est de
    // l'ordre de la microseconde, sans effet sur un classement au jour près.
    return std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        time - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
#endif
}

bool has_hidden_attribute([[maybe_unused]] const fs::path& path)
{
#ifdef _WIN32
    const DWORD attributes = ::GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_HIDDEN) != 0;
#else
    return false;
#endif
}

struct PendingDirectory {
    fs::path path;
    fs::path relative; // vide pour la racine
    bool hidden = false;
};

} // namespace

DirectorySource::DirectorySource(fs::path root) : root_(std::move(root)) {}

std::string DirectorySource::display_name() const
{
    return to_utf8(root_);
}

bool DirectorySource::is_available() const
{
    std::error_code error;
    return fs::is_directory(root_, error);
}

ScanResult DirectorySource::scan(const std::function<void(const SourceEntry&)>& on_entry,
                                 std::stop_token stop)
{
    ScanResult result;
    if (!is_available()) {
        result.outcome = ScanOutcome::source_unavailable;
        return result;
    }

    // Avant de noter une erreur, je regarde si la source est toujours là : une
    // carte retirée fait échouer tout ce qui suit, et ce n'est pas une suite
    // d'erreurs à lister mais une seule perte.
    auto record = [&](const fs::path& relative, std::error_code error) {
        if (!is_available()) {
            result.outcome = ScanOutcome::source_unavailable;
            return false;
        }
        result.errors.push_back({to_utf8(relative), error});
        return true;
    };

    // Le dossier de la racine n'entre pas dans le calcul de « caché » : sous
    // Windows, la racine d'un lecteur porte souvent cet attribut.
    std::vector<PendingDirectory> pending{{root_, {}, false}};

    while (!pending.empty()) {
        const PendingDirectory directory = std::move(pending.back());
        pending.pop_back();

        std::error_code error;
        fs::directory_iterator it(directory.path, error);
        const fs::directory_iterator end;

        for (; !error && it != end; it.increment(error)) {
            if (stop.stop_requested()) {
                result.outcome = ScanOutcome::interrupted;
                return result;
            }

            const fs::directory_entry& entry = *it;
            const fs::path relative = directory.relative / entry.path().filename();

            std::error_code entry_error;
            const fs::file_status link_status = entry.symlink_status(entry_error);
            if (entry_error) {
                if (!record(relative, entry_error)) {
                    return result;
                }
                continue;
            }

            if (fs::is_symlink(link_status)) {
                const fs::file_status target = entry.status(entry_error);
                if (entry_error) {
                    // Lien cassé.
                    if (!record(relative, entry_error)) {
                        return result;
                    }
                    continue;
                }
                if (!fs::is_regular_file(target)) {
                    continue;
                }
            }
            else if (fs::is_directory(link_status)) {
                pending.push_back({entry.path(), relative,
                                   directory.hidden || has_hidden_attribute(entry.path())});
                continue;
            }
            else if (!fs::is_regular_file(link_status)) {
                // Jonction Windows, périphérique, tube… rien à transférer.
                continue;
            }

            SourceEntry file;
            file.relative_path = to_utf8(relative);
            file.size = entry.file_size(entry_error);
            if (!entry_error) {
                file.modified = to_system_time(entry.last_write_time(entry_error));
            }
            if (entry_error) {
                if (!record(relative, entry_error)) {
                    return result;
                }
                continue;
            }
            file.hidden = directory.hidden || has_hidden_attribute(entry.path());
            on_entry(file);
        }

        if (error && !record(directory.relative, error)) {
            return result;
        }
    }

    return result;
}

std::size_t DirectorySource::read_head(std::string_view relative_path,
                                       std::span<std::byte> buffer,
                                       std::error_code& error)
{
    error.clear();
    const fs::path path = root_ / from_utf8(relative_path);

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        // Un flux ne dit pas pourquoi il n'a pas pu ouvrir : je distingue au
        // moins le fichier absent du reste.
        std::error_code status_error;
        error = fs::exists(path, status_error) ? std::make_error_code(std::errc::io_error)
                                               : std::make_error_code(std::errc::no_such_file_or_directory);
        return 0;
    }

    file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
    if (file.bad()) {
        error = std::make_error_code(std::errc::io_error);
        return 0;
    }
    return static_cast<std::size_t>(file.gcount());
}

} // namespace transfertphoto
