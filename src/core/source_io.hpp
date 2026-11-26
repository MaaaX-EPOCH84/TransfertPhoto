#pragma once

#include "core/source.hpp"

#include <exiv2/basicio.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace transfertphoto {

// Ce par quoi Exiv2 lit un fichier d'une source, quelle qu'elle soit.
//
// Je ne lui donne jamais le chemin du fichier, même pour un dossier local : il
// l'ouvre avec std::fopen, qui sous Windows ne comprend pas un chemin UTF-8
// accentué. Tout passe donc par Source::read_range, en blocs gardés en mémoire,
// Exiv2 lisant souvent octet par octet.
//
// Exiv2 demande le fichier entier (mmap) pour les formats bâtis sur TIFF, la
// plupart des RAW compris. Un fichier local est alors projeté en mémoire par
// le système, qui ne lit que les pages touchées ; sinon il est lu en entier.
//
// Lecture seule : toute écriture échoue.
class SourceIo final : public Exiv2::BasicIo {
public:
    // size : la taille annoncée par le parcours de la source.
    SourceIo(Source& source, std::string relative_path, std::uint64_t size);
    ~SourceIo() override;

    SourceIo(const SourceIo&) = delete;
    SourceIo& operator=(const SourceIo&) = delete;

    int open() override;
    int close() override;

    size_t write(const Exiv2::byte* data, size_t count) override;
    size_t write(Exiv2::BasicIo& source) override;
    int putb(Exiv2::byte data) override;
    void transfer(Exiv2::BasicIo& source) override;

    Exiv2::DataBuf read(size_t count) override;
    size_t read(Exiv2::byte* buffer, size_t count) override;
    int getb() override;
    int seek(int64_t offset, Position from) override;

    Exiv2::byte* mmap(bool writeable = false) override;
    int munmap() override;

    [[nodiscard]] size_t tell() const override;
    [[nodiscard]] size_t size() const override;
    [[nodiscard]] bool isopen() const override;
    [[nodiscard]] int error() const override;
    [[nodiscard]] bool eof() const override;
    [[nodiscard]] const std::string& path() const noexcept override;
    void populateFakeData() override;

    // La dernière erreur rendue par la source, pour dire pourquoi Exiv2 a
    // échoué : fichier disparu, téléphone débranché...
    [[nodiscard]] const std::error_code& source_error() const { return source_error_; }

    static constexpr std::size_t block_size = 64 * 1024;

private:
    class MappedFile;

    bool load_block(std::uint64_t index);

    Source& source_;
    std::string relative_path_;
    std::uint64_t size_;

    std::uint64_t position_ = 0;
    bool open_ = false;
    bool eof_ = false;
    bool failed_ = false;
    std::error_code source_error_;

    std::vector<Exiv2::byte> block_;
    std::uint64_t block_index_ = 0;
    bool block_loaded_ = false;

    // Ce que mmap() a rendu : une projection du fichier local, ou sa copie.
    std::unique_ptr<MappedFile> mapped_;
    std::vector<Exiv2::byte> whole_;
    bool whole_loaded_ = false;
};

} // namespace transfertphoto
