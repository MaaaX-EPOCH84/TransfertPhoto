#pragma once

// Fichiers à métadonnées EXIF fabriqués octet par octet, sans image : Exiv2
// n'en a pas besoin pour lire les métadonnées.

#include <array>
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

inline void put32be(std::vector<std::uint8_t>& out, std::uint32_t value)
{
    for (int shift = 24; shift >= 0; shift -= 8)
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFF));
}

// Une balise de type ASCII, le zéro final ajouté.
struct AsciiTag {
    unsigned tag;
    std::string value;
};

// Balises courantes.
inline constexpr unsigned tag_date_time = 0x0132;           // IFD0, date de retouche
inline constexpr unsigned tag_date_time_original = 0x9003;  // EXIF
inline constexpr unsigned tag_date_time_digitized = 0x9004; // EXIF
inline constexpr unsigned tag_subsec_original = 0x9291;     // EXIF
inline constexpr unsigned tag_subsec_digitized = 0x9292;    // EXIF

// Un TIFF petit-boutiste : l'IFD0 porte ses balises et le pointeur vers
// l'IFD EXIF, qui porte les siennes. Balises à donner dans l'ordre croissant.
inline std::vector<std::uint8_t> tiff_with_tags(const std::vector<AsciiTag>& ifd0,
                                                const std::vector<AsciiTag>& exif)
{
    constexpr std::uint32_t ifd0_offset = 8;
    const auto ifd_size = [](std::size_t count) { return static_cast<std::uint32_t>(2 + 12 * count + 4); };
    const std::uint32_t exif_offset = ifd0_offset + ifd_size(ifd0.size() + 1);
    std::uint32_t data_offset = exif_offset + ifd_size(exif.size());

    std::vector<std::uint8_t> tiff{'I', 'I', 0x2A, 0x00};
    put32le(tiff, ifd0_offset);
    std::vector<std::uint8_t> data;

    // Une valeur de quatre octets au plus tient dans l'entrée elle-même.
    const auto put_entry = [&](const AsciiTag& entry) {
        std::vector<std::uint8_t> value(entry.value.begin(), entry.value.end());
        value.push_back(0);
        put16le(tiff, entry.tag);
        put16le(tiff, 2); // ASCII
        put32le(tiff, static_cast<std::uint32_t>(value.size()));
        if (value.size() <= 4) {
            value.resize(4, 0);
            tiff.insert(tiff.end(), value.begin(), value.end());
        } else {
            put32le(tiff, data_offset + static_cast<std::uint32_t>(data.size()));
            data.insert(data.end(), value.begin(), value.end());
            if (data.size() % 2 != 0)
                data.push_back(0);
        }
    };

    put16le(tiff, static_cast<unsigned>(ifd0.size() + 1));
    bool pointer_written = false;
    const auto put_pointer = [&] {
        put16le(tiff, 0x8769); // ExifIFDPointer
        put16le(tiff, 4);      // LONG
        put32le(tiff, 1);
        put32le(tiff, exif_offset);
        pointer_written = true;
    };
    for (const auto& entry : ifd0) {
        if (!pointer_written && entry.tag > 0x8769)
            put_pointer();
        put_entry(entry);
    }
    if (!pointer_written)
        put_pointer();
    put32le(tiff, 0);

    put16le(tiff, static_cast<unsigned>(exif.size()));
    for (const auto& entry : exif)
        put_entry(entry);
    put32le(tiff, 0);

    tiff.insert(tiff.end(), data.begin(), data.end());
    return tiff;
}

inline std::vector<std::uint8_t> tiff_with_capture_date(const std::string& date)
{
    return tiff_with_tags({}, {{tag_date_time_original, date}});
}

// Un JPEG réduit à son bloc APP1, qui porte le TIFF.
inline std::vector<std::uint8_t> jpeg_with_tiff(const std::vector<std::uint8_t>& tiff)
{
    std::vector<std::uint8_t> jpeg{0xFF, 0xD8, 0xFF, 0xE1};
    const auto length = static_cast<unsigned>(2 + 6 + tiff.size());
    jpeg.push_back(static_cast<std::uint8_t>(length >> 8));
    jpeg.push_back(static_cast<std::uint8_t>(length & 0xFF));
    jpeg.insert(jpeg.end(), {'E', 'x', 'i', 'f', 0, 0});
    jpeg.insert(jpeg.end(), tiff.begin(), tiff.end());
    jpeg.insert(jpeg.end(), {0xFF, 0xD9});
    return jpeg;
}

inline std::vector<std::uint8_t> jpeg_with_capture_date(const std::string& date)
{
    return jpeg_with_tiff(tiff_with_capture_date(date));
}

// Un JPEG sans aucune métadonnée.
inline std::vector<std::uint8_t> bare_jpeg()
{
    return {0xFF, 0xD8, 0xFF, 0xD9};
}

inline std::uint32_t crc32(const std::uint8_t* data, std::size_t size)
{
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

inline void put_png_chunk(std::vector<std::uint8_t>& png, const char (&type)[5],
                          const std::vector<std::uint8_t>& data)
{
    put32be(png, static_cast<std::uint32_t>(data.size()));
    std::vector<std::uint8_t> body(type, type + 4);
    body.insert(body.end(), data.begin(), data.end());
    png.insert(png.end(), body.begin(), body.end());
    put32be(png, crc32(body.data(), body.size()));
}

// Un PNG d'un pixel gris, dont le bloc eXIf porte le TIFF.
inline std::vector<std::uint8_t> png_with_tiff(const std::vector<std::uint8_t>& tiff)
{
    std::vector<std::uint8_t> png{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<std::uint8_t> header;
    put32be(header, 1);                 // largeur
    put32be(header, 1);                 // hauteur
    header.insert(header.end(), {8, 0, 0, 0, 0}); // 8 bits, gris, sans entrelacement
    put_png_chunk(png, "IHDR", header);
    put_png_chunk(png, "eXIf", tiff);
    put_png_chunk(png, "IEND", {});
    return png;
}

// Un WebP sans image : un bloc VP8X qui annonce l'EXIF, puis le bloc EXIF.
inline std::vector<std::uint8_t> webp_with_tiff(const std::vector<std::uint8_t>& tiff)
{
    std::vector<std::uint8_t> chunks{'W', 'E', 'B', 'P', 'V', 'P', '8', 'X'};
    put32le(chunks, 10);
    chunks.insert(chunks.end(), {0x08, 0, 0, 0, 0, 0, 0, 0, 0, 0}); // drapeau EXIF, 1 × 1
    chunks.insert(chunks.end(), {'E', 'X', 'I', 'F'});
    put32le(chunks, static_cast<std::uint32_t>(tiff.size()));
    chunks.insert(chunks.end(), tiff.begin(), tiff.end());
    if (tiff.size() % 2 != 0)
        chunks.push_back(0);

    std::vector<std::uint8_t> webp{'R', 'I', 'F', 'F'};
    put32le(webp, static_cast<std::uint32_t>(chunks.size()));
    webp.insert(webp.end(), chunks.begin(), chunks.end());
    return webp;
}

} // namespace test_support
