#include "core/source_io.hpp"

#include <exiv2/error.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <span>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace transfertphoto {

// Un fichier local projeté en mémoire, en lecture seule. Le système ne lit
// que les pages qu'on touche.
class SourceIo::MappedFile {
public:
    MappedFile() = default;
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;
    ~MappedFile() { unmap(); }

    // Faux si le fichier ne s'ouvre pas ou ne se projette pas.
    bool map(const std::filesystem::path& path)
    {
#ifdef _WIN32
        const HANDLE file = ::CreateFileW(path.c_str(), GENERIC_READ,
                                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                          nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return false;
        LARGE_INTEGER length{};
        if (!::GetFileSizeEx(file, &length)) {
            ::CloseHandle(file);
            return false;
        }
        size_ = static_cast<std::uint64_t>(length.QuadPart);
        // Un fichier vide ne se projette pas ; il n'y a rien à lire.
        if (size_ == 0) {
            ::CloseHandle(file);
            return true;
        }
        const HANDLE mapping = ::CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
        ::CloseHandle(file);
        if (mapping == nullptr)
            return false;
        // La vue garde la projection en vie après la fermeture des descripteurs.
        data_ = static_cast<const Exiv2::byte*>(::MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
        ::CloseHandle(mapping);
        return data_ != nullptr;
#else
        const int file = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
        if (file < 0)
            return false;
        struct stat status {};
        if (::fstat(file, &status) != 0) {
            ::close(file);
            return false;
        }
        size_ = static_cast<std::uint64_t>(status.st_size);
        if (size_ == 0) {
            ::close(file);
            return true;
        }
        void* address = ::mmap(nullptr, static_cast<std::size_t>(size_), PROT_READ, MAP_PRIVATE, file, 0);
        ::close(file);
        if (address == MAP_FAILED)
            return false;
        data_ = static_cast<const Exiv2::byte*>(address);
        return true;
#endif
    }

    void unmap()
    {
        if (data_ != nullptr) {
#ifdef _WIN32
            ::UnmapViewOfFile(data_);
#else
            ::munmap(const_cast<Exiv2::byte*>(data_), static_cast<std::size_t>(size_));
#endif
        }
        data_ = nullptr;
        size_ = 0;
    }

    [[nodiscard]] const Exiv2::byte* data() const { return data_; }
    [[nodiscard]] std::uint64_t size() const { return size_; }

private:
    const Exiv2::byte* data_ = nullptr;
    std::uint64_t size_ = 0;
};

SourceIo::SourceIo(Source& source, std::string relative_path, std::uint64_t size)
    : source_(source), relative_path_(std::move(relative_path)), size_(size)
{
}

SourceIo::~SourceIo() = default;

int SourceIo::open()
{
    open_ = true;
    position_ = 0;
    eof_ = false;
    failed_ = false;
    return 0;
}

int SourceIo::close()
{
    munmap();
    open_ = false;
    return 0;
}

size_t SourceIo::write(const Exiv2::byte* /*data*/, size_t /*count*/)
{
    return 0;
}

size_t SourceIo::write(Exiv2::BasicIo& /*source*/)
{
    return 0;
}

int SourceIo::putb(Exiv2::byte /*data*/)
{
    return EOF;
}

void SourceIo::transfer(Exiv2::BasicIo& /*source*/)
{
    throw Exiv2::Error(Exiv2::ErrorCode::kerFunctionNotSupported, "SourceIo::transfer");
}

bool SourceIo::load_block(std::uint64_t index)
{
    if (block_loaded_ && block_index_ == index)
        return true;

    block_.resize(block_size);
    std::error_code error;
    const auto count = source_.read_range(relative_path_, index * block_size,
                                          std::as_writable_bytes(std::span(block_)), error);
    if (error) {
        source_error_ = error;
        block_loaded_ = false;
        return false;
    }
    block_.resize(count);
    block_index_ = index;
    block_loaded_ = true;
    return true;
}

size_t SourceIo::read(Exiv2::byte* buffer, size_t count)
{
    size_t done = 0;
    while (done < count) {
        if (position_ >= size_) {
            eof_ = true;
            break;
        }
        const std::uint64_t index = position_ / block_size;
        if (!load_block(index)) {
            failed_ = true;
            break;
        }
        const auto start = static_cast<std::size_t>(position_ - index * block_size);
        // Fichier plus court que la taille annoncée : il a changé depuis le
        // parcours. On s'arrête là comme à une fin de fichier.
        if (start >= block_.size()) {
            eof_ = true;
            break;
        }
        const auto available = static_cast<std::uint64_t>(block_.size() - start);
        const auto step = static_cast<std::size_t>(
            std::min({static_cast<std::uint64_t>(count - done), available, size_ - position_}));
        std::memcpy(buffer + done, block_.data() + start, step);
        done += step;
        position_ += step;
    }
    return done;
}

Exiv2::DataBuf SourceIo::read(size_t count)
{
    Exiv2::DataBuf buffer(count);
    const auto done = read(buffer.data(), count);
    buffer.resize(done);
    return buffer;
}

int SourceIo::getb()
{
    Exiv2::byte value = 0;
    return read(&value, 1) == 1 ? value : EOF;
}

int SourceIo::seek(int64_t offset, Position from)
{
    int64_t base = 0;
    switch (from) {
    case Position::beg:
        base = 0;
        break;
    case Position::cur:
        base = static_cast<int64_t>(position_);
        break;
    case Position::end:
        base = static_cast<int64_t>(size_);
        break;
    }
    // Les décalages viennent du fichier lu : ils peuvent être absurdes.
    if ((offset > 0 && base > std::numeric_limits<int64_t>::max() - offset))
        return 1;
    const int64_t target = base + offset;
    if (target < 0)
        return 1;
    if (static_cast<std::uint64_t>(target) > size_) {
        eof_ = true;
        return 1;
    }
    position_ = static_cast<std::uint64_t>(target);
    eof_ = false;
    return 0;
}

Exiv2::byte* SourceIo::mmap(bool writeable)
{
    if (writeable)
        throw Exiv2::Error(Exiv2::ErrorCode::kerFunctionNotSupported, "SourceIo::mmap");

    // Exiv2 ne fait qu'y lire : la projection est en lecture seule, et la copie
    // n'est jamais modifiée.
    if (mapped_)
        return const_cast<Exiv2::byte*>(mapped_->data());
    if (whole_loaded_)
        return whole_.data();

    if (const auto local = source_.local_path(relative_path_)) {
        auto mapped = std::make_unique<MappedFile>();
        if (mapped->map(*local)) {
            // Un fichier qui n'a plus la taille annoncée ferait lire Exiv2
            // au-delà de la projection.
            if (mapped->size() != size_)
                throw Exiv2::Error(Exiv2::ErrorCode::kerFailedToReadImageData);
            mapped_ = std::move(mapped);
            return const_cast<Exiv2::byte*>(mapped_->data());
        }
    }

    if (size_ > std::numeric_limits<std::size_t>::max())
        throw Exiv2::Error(Exiv2::ErrorCode::kerFailedToReadImageData);
    whole_.resize(static_cast<std::size_t>(size_));
    std::error_code error;
    const auto count = source_.read_range(relative_path_, 0,
                                          std::as_writable_bytes(std::span(whole_)), error);
    if (error || count != whole_.size()) {
        if (error)
            source_error_ = error;
        whole_.clear();
        throw Exiv2::Error(Exiv2::ErrorCode::kerFailedToReadImageData);
    }
    whole_loaded_ = true;
    return whole_.data();
}

int SourceIo::munmap()
{
    mapped_.reset();
    whole_.clear();
    whole_.shrink_to_fit();
    whole_loaded_ = false;
    return 0;
}

size_t SourceIo::tell() const
{
    return static_cast<size_t>(position_);
}

size_t SourceIo::size() const
{
    return static_cast<size_t>(size_);
}

bool SourceIo::isopen() const
{
    return open_;
}

int SourceIo::error() const
{
    return failed_ ? 1 : 0;
}

bool SourceIo::eof() const
{
    return eof_;
}

const std::string& SourceIo::path() const noexcept
{
    return relative_path_;
}

void SourceIo::populateFakeData()
{
}

} // namespace transfertphoto
