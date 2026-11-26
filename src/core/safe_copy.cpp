#include "core/safe_copy.hpp"

#include "core/directory_source.hpp"

#define XXH_INLINE_ALL
#include <xxhash.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace transfertphoto {
namespace {

fs::path from_utf8(std::string_view text)
{
    return fs::path(std::u8string(text.begin(), text.end()));
}

std::error_code last_error()
{
#ifdef _WIN32
    return {static_cast<int>(::GetLastError()), std::system_category()};
#else
    return {errno, std::generic_category()};
#endif
}

// Un fichier ouvert en écriture, que l'on force sur le disque avant de le
// fermer : la source d'un déplacement ne s'efface qu'une fois la copie sûre.
class OutputFile {
public:
    explicit OutputFile(const fs::path& path, std::error_code& error)
    {
#ifdef _WIN32
        handle_ = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE)
            error = last_error();
#else
        descriptor_ = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
        if (descriptor_ < 0)
            error = last_error();
#endif
    }

    OutputFile(const OutputFile&) = delete;
    OutputFile& operator=(const OutputFile&) = delete;
    ~OutputFile() { (void)close(); }

    bool write(std::span<const std::byte> data, std::error_code& error)
    {
        while (!data.empty()) {
#ifdef _WIN32
            DWORD written = 0;
            const auto chunk = static_cast<DWORD>(std::min<std::size_t>(data.size(), 1u << 30));
            if (!::WriteFile(handle_, data.data(), chunk, &written, nullptr)) {
                error = last_error();
                return false;
            }
#else
            const auto written = ::write(descriptor_, data.data(), data.size());
            if (written < 0) {
                if (errno == EINTR)
                    continue;
                error = last_error();
                return false;
            }
#endif
            data = data.subspan(static_cast<std::size_t>(written));
        }
        return true;
    }

    // Force les données sur le disque, puis ferme.
    bool sync_and_close(std::error_code& error)
    {
#ifdef _WIN32
        if (!::FlushFileBuffers(handle_)) {
            error = last_error();
            return false;
        }
#else
        if (::fsync(descriptor_) != 0) {
            error = last_error();
            return false;
        }
#endif
        if (!close()) {
            error = last_error();
            return false;
        }
        return true;
    }

private:
    bool close()
    {
#ifdef _WIN32
        if (handle_ == INVALID_HANDLE_VALUE)
            return true;
        const bool closed = ::CloseHandle(handle_) != 0;
        handle_ = INVALID_HANDLE_VALUE;
        return closed;
#else
        if (descriptor_ < 0)
            return true;
        const bool closed = ::close(descriptor_) == 0;
        descriptor_ = -1;
        return closed;
#endif
    }

#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int descriptor_ = -1;
#endif
};

fs::file_time_type to_file_time(std::chrono::system_clock::time_point time)
{
#if defined(__cpp_lib_chrono) && __cpp_lib_chrono >= 201907L
    return std::chrono::clock_cast<fs::file_time_type::clock>(time);
#else
    // Sans clock_cast, comme dans directory_source.cpp : l'écart est de
    // l'ordre de la microseconde.
    return std::chrono::time_point_cast<fs::file_time_type::duration>(
        time - std::chrono::system_clock::now() + fs::file_time_type::clock::now());
#endif
}

struct StateDeleter {
    void operator()(XXH3_state_t* state) const noexcept { XXH3_freeState(state); }
};

CopyResult failure(CopyResult::Outcome outcome, std::error_code error = {})
{
    CopyResult result;
    result.outcome = outcome;
    result.error = error;
    return result;
}

#if !defined(_WIN32)
// Le dernier recours, quand le système de fichiers ne sait renommer sans
// remplacer ni créer de lien (certains partages réseau) : vérifier puis
// renommer, avec une brève fenêtre où un autre programme pourrait créer le
// même nom.
RenameOutcome check_then_rename(const fs::path& from, const fs::path& to, std::error_code& error)
{
    if (fs::exists(to, error))
        return RenameOutcome::target_exists;
    if (std::rename(from.c_str(), to.c_str()) != 0) {
        error = last_error();
        return RenameOutcome::failed;
    }
    return RenameOutcome::renamed;
}

// Un lien dur échoue si le nom existe ; on retire ensuite l'ancien nom.
RenameOutcome link_then_unlink(const fs::path& from, const fs::path& to, std::error_code& error)
{
    if (::link(from.c_str(), to.c_str()) == 0) {
        ::unlink(from.c_str());
        return RenameOutcome::renamed;
    }
    if (errno == EEXIST)
        return RenameOutcome::target_exists;
    return check_then_rename(from, to, error);
}
#endif

} // namespace

std::string temporary_name(std::string_view name)
{
    return "." + std::string(name) + std::string(temporary_suffix);
}

RenameOutcome rename_without_replacing(const fs::path& from, const fs::path& to, std::error_code& error)
{
    error.clear();
#ifdef _WIN32
    // Sans MOVEFILE_REPLACE_EXISTING, MoveFileExW refuse un nom qui existe.
    if (::MoveFileExW(from.c_str(), to.c_str(), 0))
        return RenameOutcome::renamed;
    const DWORD code = ::GetLastError();
    if (code == ERROR_ALREADY_EXISTS || code == ERROR_FILE_EXISTS)
        return RenameOutcome::target_exists;
    error = {static_cast<int>(code), std::system_category()};
    return RenameOutcome::failed;
#elif defined(__linux__)
    if (::renameat2(AT_FDCWD, from.c_str(), AT_FDCWD, to.c_str(), RENAME_NOREPLACE) == 0)
        return RenameOutcome::renamed;
    if (errno == EEXIST)
        return RenameOutcome::target_exists;
    // Système de fichiers qui ignore l'option (certains montages réseau).
    if (errno == EINVAL || errno == ENOSYS || errno == ENOTSUP)
        return link_then_unlink(from, to, error);
    error = last_error();
    return RenameOutcome::failed;
#elif defined(__APPLE__)
    if (::renamex_np(from.c_str(), to.c_str(), RENAME_EXCL) == 0)
        return RenameOutcome::renamed;
    if (errno == EEXIST)
        return RenameOutcome::target_exists;
    if (errno == ENOTSUP || errno == EINVAL)
        return link_then_unlink(from, to, error);
    error = last_error();
    return RenameOutcome::failed;
#else
    return link_then_unlink(from, to, error);
#endif
}

bool matches_hash(const fs::path& file, std::uint64_t size, const Hash128& expected, std::stop_token stop,
                  std::error_code& error, const std::function<void(std::uint64_t)>& on_bytes)
{
    DirectorySource folder(file.parent_path());
    const auto name = file.filename().u8string();
    const auto result = hash_file(folder, std::string(name.begin(), name.end()), size, stop, whole_file, on_bytes);
    error = result.source_error;
    return result.hash && *result.hash == expected;
}

CopyResult copy_safely(const CopyRequest& request, std::stop_token stop,
                       const std::function<void(std::uint64_t)>& on_bytes)
{
    std::error_code error;
    fs::create_directories(request.folder, error);
    if (error)
        return failure(CopyResult::Outcome::write_failed, error);

    const fs::path final_path = request.folder / from_utf8(request.name);
    const fs::path temporary = request.folder / from_utf8(temporary_name(request.name));
    // Un nom déjà pris n'est même pas tenté : rien à écrire pour rien.
    if (fs::exists(final_path, error))
        return failure(CopyResult::Outcome::name_taken);

    const auto discard = [&](CopyResult result) {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        return result;
    };

    const std::unique_ptr<XXH3_state_t, StateDeleter> state(XXH3_createState());
    if (!state || XXH3_128bits_reset(state.get()) != XXH_OK)
        return failure(CopyResult::Outcome::write_failed, std::make_error_code(std::errc::not_enough_memory));

    // L'écriture dans un bloc à part : le fichier est fermé avant d'être
    // effacé, ce que Windows exige.
    const auto write_failure = [&]() -> std::optional<CopyResult> {
        OutputFile output(temporary, error);
        if (error)
            return failure(CopyResult::Outcome::write_failed, error);

        std::vector<std::byte> buffer(
            static_cast<std::size_t>(std::min<std::uint64_t>(request.size, hash_block_size)));
        for (std::uint64_t done = 0; done < request.size;) {
            if (stop.stop_requested())
                return failure(CopyResult::Outcome::interrupted);
            const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(request.size - done, buffer.size()));
            const auto read =
                request.source.read_range(request.relative_path, done, std::span(buffer.data(), count), error);
            if (error)
                return failure(CopyResult::Outcome::read_failed, error);
            if (read == 0)
                return failure(CopyResult::Outcome::changed);
            (void)XXH3_128bits_update(state.get(), buffer.data(), read);
            if (!output.write(std::span(buffer.data(), read), error))
                return failure(CopyResult::Outcome::write_failed, error);
            done += read;
            if (on_bytes)
                on_bytes(read);
        }
        // Un octet de plus que prévu : l'original a grandi depuis l'inventaire.
        std::byte probe{};
        if (request.source.read_range(request.relative_path, request.size, std::span(&probe, 1), error) != 0)
            return failure(CopyResult::Outcome::changed);
        if (error)
            return failure(CopyResult::Outcome::read_failed, error);
        if (!output.sync_and_close(error))
            return failure(CopyResult::Outcome::write_failed, error);
        return std::nullopt;
    }();
    if (write_failure)
        return discard(*write_failure);

    fs::last_write_time(temporary, to_file_time(request.modified), error);
    if (error)
        return discard(failure(CopyResult::Outcome::write_failed, error));

    switch (rename_without_replacing(temporary, final_path, error)) {
    case RenameOutcome::renamed:
        break;
    case RenameOutcome::target_exists:
        return discard(failure(CopyResult::Outcome::name_taken));
    case RenameOutcome::failed:
        return discard(failure(CopyResult::Outcome::write_failed, error));
    }

    CopyResult result;
    const auto digest = XXH3_128bits_digest(state.get());
    result.hash = {digest.high64, digest.low64};
    result.path = final_path;
    if (request.verify) {
        if (!matches_hash(final_path, request.size, result.hash, stop, error, on_bytes)) {
            // Un arrêt pendant la relecture n'accuse pas la copie, mais ne
            // l'a pas vérifiée : on ne la garde pas comme sûre.
            std::error_code ignored;
            fs::remove(final_path, ignored);
            return failure(stop.stop_requested() ? CopyResult::Outcome::interrupted
                                                 : CopyResult::Outcome::verify_failed,
                           error);
        }
        result.verified = true;
    }
    return result;
}

std::size_t remove_leftover_temporaries(const fs::path& root)
{
    const std::u8string suffix(temporary_suffix.begin(), temporary_suffix.end());
    std::vector<fs::path> leftovers;
    std::error_code error;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, error), end;
         !error && it != end; it.increment(error)) {
        const auto name = it->path().filename().u8string();
        std::error_code ignored;
        if (name.size() > suffix.size() + 1 && name.front() == u8'.' && name.ends_with(suffix)
            && it->is_regular_file(ignored))
            leftovers.push_back(it->path());
    }
    std::size_t removed = 0;
    for (const auto& path : leftovers) {
        std::error_code ignored;
        if (fs::remove(path, ignored))
            ++removed;
    }
    return removed;
}

} // namespace transfertphoto
