#include "core/trash.hpp"

#include <array>
#include <cstdio>
#include <optional>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
#elif defined(__linux__)
#include <cerrno>
#include <cstdlib>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace transfertphoto {

namespace trash_detail {

bool is_network_filesystem(std::string_view type) noexcept
{
    constexpr std::array<std::string_view, 14> network{"nfs",       "nfs4",       "cifs",         "smb3",
                                                       "smbfs",     "afs",        "9p",           "ncpfs",
                                                       "davfs",     "fuse.sshfs", "fuse.davfs2",  "fuse.rclone",
                                                       "glusterfs", "ceph"};
    for (const auto known : network) {
        if (type == known)
            return true;
    }
    // Les montages de GVFS : téléphones en MTP, partages réseau.
    return type.starts_with("fuse.gvfs");
}

std::string encode_trash_path(std::string_view utf8_path)
{
    std::string encoded;
    for (const char c : utf8_path) {
        const auto byte = static_cast<unsigned char>(c);
        const bool unreserved = (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z')
                                || (byte >= '0' && byte <= '9') || byte == '-' || byte == '_' || byte == '.'
                                || byte == '~' || byte == '/';
        if (unreserved) {
            encoded += c;
        } else {
            std::array<char, 4> hex{};
            std::snprintf(hex.data(), hex.size(), "%%%02X", byte);
            encoded += hex.data();
        }
    }
    return encoded;
}

} // namespace trash_detail

std::error_code delete_permanently(const fs::path& file)
{
    std::error_code error;
    if (!fs::remove(file, error) && !error)
        error = std::make_error_code(std::errc::no_such_file_or_directory);
    return error;
}

#if defined(_WIN32)

namespace {

std::wstring volume_root(const fs::path& file)
{
    std::array<wchar_t, MAX_PATH + 1> root{};
    if (!::GetVolumePathNameW(fs::absolute(file).c_str(), root.data(), static_cast<DWORD>(root.size())))
        return {};
    return root.data();
}

// COM pour le fil courant, le temps de l'opération.
class ComScope {
public:
    ComScope() : result_(::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)) {}
    ~ComScope()
    {
        if (SUCCEEDED(result_))
            ::CoUninitialize();
    }
    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;

    // Déjà initialisé par l'appelant dans un autre modèle : on fait avec.
    [[nodiscard]] bool usable() const { return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE; }
    [[nodiscard]] HRESULT result() const { return result_; }

private:
    HRESULT result_;
};

std::error_code from_hresult(HRESULT result)
{
    return {static_cast<int>(result), std::system_category()};
}

} // namespace

bool volume_has_trash(const fs::path& file)
{
    const auto root = volume_root(file);
    if (root.empty())
        return false;
    const UINT type = ::GetDriveTypeW(root.c_str());
    // Les clés et les cartes n'ont pas de corbeille sous Windows ; les
    // partages réseau non plus.
    return type == DRIVE_FIXED || type == DRIVE_RAMDISK;
}

TrashResult move_to_trash(const fs::path& file)
{
    TrashResult result;
    if (!volume_has_trash(file)) {
        // Sans corbeille, FOFX_RECYCLEONDELETE supprimerait pour de bon.
        result.outcome = TrashOutcome::unavailable;
        return result;
    }

    const ComScope com;
    if (!com.usable()) {
        result.error = from_hresult(com.result());
        return result;
    }

    IFileOperation* operation = nullptr;
    HRESULT hr = ::CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&operation));
    if (FAILED(hr)) {
        result.error = from_hresult(hr);
        return result;
    }
    hr = operation->SetOperationFlags(FOF_NO_UI | FOFX_RECYCLEONDELETE | FOFX_EARLYFAILURE);
    IShellItem* item = nullptr;
    if (SUCCEEDED(hr))
        hr = ::SHCreateItemFromParsingName(fs::absolute(file).c_str(), nullptr, IID_PPV_ARGS(&item));
    if (SUCCEEDED(hr))
        hr = operation->DeleteItem(item, nullptr);
    if (SUCCEEDED(hr))
        hr = operation->PerformOperations();
    BOOL aborted = FALSE;
    if (SUCCEEDED(hr) && SUCCEEDED(operation->GetAnyOperationsAborted(&aborted)) && aborted)
        hr = E_ABORT;
    if (item != nullptr)
        item->Release();
    operation->Release();

    if (FAILED(hr)) {
        result.error = from_hresult(hr);
        return result;
    }
    result.outcome = TrashOutcome::trashed;
    return result;
}

#elif defined(__linux__)

namespace {

std::error_code last_error()
{
    return {errno, std::generic_category()};
}

std::string to_utf8(const fs::path& path)
{
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

struct MountInfo {
    std::string mount_point;
    std::string type;
    std::string source;
};

// L'entrée de /proc/self/mountinfo du périphérique de ce fichier.
std::optional<MountInfo> mount_of(dev_t device)
{
    std::ifstream mounts("/proc/self/mountinfo");
    const std::string wanted = std::to_string(major(device)) + ":" + std::to_string(minor(device));
    std::string line;
    std::optional<MountInfo> found;
    while (std::getline(mounts, line)) {
        std::istringstream fields(line);
        std::string id, parent, numbers, root, mount_point;
        fields >> id >> parent >> numbers >> root >> mount_point;
        if (numbers != wanted)
            continue;
        // Après les options facultatives vient « - », puis le type et la source.
        std::string field;
        while (fields >> field && field != "-") {
        }
        MountInfo info;
        info.mount_point = mount_point;
        fields >> info.type >> info.source;
        found = info; // la dernière l'emporte, comme les montages superposés
    }
    return found;
}

// Un périphérique amovible : marqué tel, carte SD, ou branché par USB.
bool is_removable_device(const std::string& source)
{
    if (!source.starts_with("/dev/"))
        return false;
    const std::string name = fs::path(source).filename().string();
    if (name.starts_with("mmcblk"))
        return true;
    std::error_code error;
    const fs::path block = fs::path("/sys/class/block") / name;
    const auto resolved = fs::canonical(block, error);
    if (!error && resolved.string().find("/usb") != std::string::npos)
        return true;
    for (const auto& candidate : {block / "removable", block / ".." / "removable"}) {
        std::ifstream flag(candidate);
        char value = '0';
        if (flag >> value && value == '1')
            return true;
    }
    return false;
}

fs::path home_trash()
{
    if (const char* data = std::getenv("XDG_DATA_HOME"); data != nullptr && *data == '/')
        return fs::path(data) / "Trash";
    const char* home = std::getenv("HOME");
    return fs::path(home != nullptr ? home : "/") / ".local" / "share" / "Trash";
}

std::optional<dev_t> device_of(const fs::path& path)
{
    struct stat status {};
    if (::lstat(path.c_str(), &status) != 0)
        return std::nullopt;
    return status.st_dev;
}

// Le point de montage : le plus haut parent sur le même périphérique.
fs::path top_directory(const fs::path& file, dev_t device)
{
    fs::path top = file.parent_path();
    while (top.has_parent_path() && top != top.parent_path()) {
        const auto parent_device = device_of(top.parent_path());
        if (!parent_device || *parent_device != device)
            break;
        top = top.parent_path();
    }
    return top;
}

// $topdir/.Trash/$uid si l'administrateur l'a préparé comme le veut la
// spécification (dossier, bit collant, pas un lien), sinon $topdir/.Trash-$uid.
std::optional<fs::path> volume_trash(const fs::path& top)
{
    const auto uid = std::to_string(::getuid());
    struct stat shared {};
    const fs::path shared_dir = top / ".Trash";
    if (::lstat(shared_dir.c_str(), &shared) == 0 && S_ISDIR(shared.st_mode) && (shared.st_mode & S_ISVTX)) {
        const fs::path mine = shared_dir / uid;
        if (::mkdir(mine.c_str(), 0700) == 0 || errno == EEXIST)
            return mine;
    }
    const fs::path own = top / (".Trash-" + uid);
    if (::mkdir(own.c_str(), 0700) == 0 || errno == EEXIST)
        return own;
    return std::nullopt;
}

bool make_trash_directories(const fs::path& trash)
{
    std::error_code error;
    fs::create_directories(trash / "files", error);
    if (error)
        return false;
    fs::create_directories(trash / "info", error);
    if (error)
        return false;
    fs::permissions(trash, fs::perms::owner_all, fs::perm_options::replace, error);
    return true;
}

std::string deletion_date()
{
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    ::localtime_r(&now, &local);
    std::array<char, 32> text{};
    std::strftime(text.data(), text.size(), "%Y-%m-%dT%H:%M:%S", &local);
    return text.data();
}

} // namespace

bool volume_has_trash(const fs::path& file)
{
    std::error_code error;
    const auto absolute = fs::absolute(file, error);
    const auto device = device_of(absolute);
    if (error || !device)
        return false;
    const auto mount = mount_of(*device);
    if (!mount)
        return true; // rien d'inquiétant à en dire
    return !trash_detail::is_network_filesystem(mount->type) && !is_removable_device(mount->source);
}

TrashResult move_to_trash(const fs::path& file)
{
    TrashResult result;
    std::error_code error;
    const auto absolute = fs::absolute(file, error).lexically_normal();
    const auto device = device_of(absolute);
    if (error || !device) {
        result.error = error ? error : std::make_error_code(std::errc::no_such_file_or_directory);
        return result;
    }
    if (!volume_has_trash(absolute)) {
        result.outcome = TrashOutcome::unavailable;
        return result;
    }

    // La corbeille de l'utilisateur s'il est sur son disque, sinon celle du
    // disque, où le chemin noté est relatif à sa racine.
    fs::path trash = home_trash();
    std::string noted_path = to_utf8(absolute);
    std::error_code ignored;
    fs::create_directories(trash, ignored);
    const auto trash_device = device_of(trash);
    if (!trash_device || *trash_device != *device) {
        const auto top = top_directory(absolute, *device);
        const auto own = volume_trash(top);
        if (!own) {
            result.outcome = TrashOutcome::unavailable;
            return result;
        }
        trash = *own;
        noted_path = to_utf8(absolute.lexically_relative(top));
    }
    if (!make_trash_directories(trash)) {
        result.error = last_error();
        return result;
    }

    // Un nom libre, réservé par la création exclusive de sa fiche .trashinfo.
    const auto name = absolute.filename();
    const auto stem = name.stem().string();
    const auto extension = name.extension().string();
    for (unsigned number = 1; number < 100'000; ++number) {
        const std::string candidate =
            number == 1 ? name.string() : stem + " (" + std::to_string(number) + ")" + extension;
        const fs::path info = trash / "info" / (candidate + ".trashinfo");
        const int descriptor = ::open(info.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (descriptor < 0) {
            if (errno == EEXIST)
                continue;
            result.error = last_error();
            return result;
        }
        const std::string content = "[Trash Info]\nPath=" + trash_detail::encode_trash_path(noted_path)
                                    + "\nDeletionDate=" + deletion_date() + "\n";
        const bool written = ::write(descriptor, content.data(), content.size())
                             == static_cast<ssize_t>(content.size());
        ::close(descriptor);
        const fs::path target = trash / "files" / candidate;
        if (!written || ::rename(absolute.c_str(), target.c_str()) != 0) {
            result.error = written ? last_error() : std::make_error_code(std::errc::io_error);
            ::unlink(info.c_str());
            return result;
        }
        result.outcome = TrashOutcome::trashed;
        return result;
    }
    result.error = std::make_error_code(std::errc::file_exists);
    return result;
}

#elif !defined(__APPLE__)

// Ni Windows, ni macOS, ni Linux : pas de corbeille connue.
bool volume_has_trash(const fs::path&)
{
    return false;
}

TrashResult move_to_trash(const fs::path&)
{
    return {TrashOutcome::unavailable, {}};
}

#endif

} // namespace transfertphoto
