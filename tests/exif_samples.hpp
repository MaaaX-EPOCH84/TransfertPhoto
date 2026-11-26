#pragma once

// Fichiers à métadonnées EXIF fabriqués octet par octet, sans image : Exiv2
// n'en a pas besoin pour lire les métadonnées.

#include <cstdint>
#include <string>
#include <vector>

namespace test_support {

inline void put16le(std::vector<std::uint8_t>& out, unsigned value)
{
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
}

inline void put32le(std::vector<std::uint8_t>& out, std::uint32_t value)
{
    put16le(out, value & 0xFFFF);
    put16le(out, value >> 16);
}

// Un TIFF petit-boutiste : un IFD0 qui ne contient que le pointeur vers l'IFD
// EXIF, et celui-ci la seule date de prise de vue.
inline std::vector<std::uint8_t> tiff_with_capture_date(const std::string& date)
{
    constexpr std::uint32_t ifd0 = 8;
    constexpr std::uint32_t exif_ifd = ifd0 + 2 + 12 + 4;
    constexpr std::uint32_t date_offset = exif_ifd + 2 + 12 + 4;

    std::vector<std::uint8_t> tiff{'I', 'I', 0x2A, 0x00};
    put32le(tiff, ifd0);

    put16le(tiff, 1);
    put16le(tiff, 0x8769); // ExifIFDPointer
    put16le(tiff, 4);      // LONG
    put32le(tiff, 1);
    put32le(tiff, exif_ifd);
    put32le(tiff, 0);

    put16le(tiff, 1);
    put16le(tiff, 0x9003); // DateTimeOriginal
    put16le(tiff, 2);      // ASCII, zéro final compris
    put32le(tiff, static_cast<std::uint32_t>(date.size() + 1));
    put32le(tiff, date_offset);
    put32le(tiff, 0);

    tiff.insert(tiff.end(), date.begin(), date.end());
    tiff.push_back(0);
    return tiff;
}

// Un JPEG réduit à son bloc APP1, qui porte le TIFF ci-dessus.
inline std::vector<std::uint8_t> jpeg_with_capture_date(const std::string& date)
{
    const auto tiff = tiff_with_capture_date(date);
    std::vector<std::uint8_t> jpeg{0xFF, 0xD8, 0xFF, 0xE1};
    const auto length = static_cast<unsigned>(2 + 6 + tiff.size());
    jpeg.push_back(static_cast<std::uint8_t>(length >> 8));
    jpeg.push_back(static_cast<std::uint8_t>(length & 0xFF));
    jpeg.insert(jpeg.end(), {'E', 'x', 'i', 'f', 0, 0});
    jpeg.insert(jpeg.end(), tiff.begin(), tiff.end());
    jpeg.insert(jpeg.end(), {0xFF, 0xD9});
    return jpeg;
}

} // namespace test_support
