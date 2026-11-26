#include <exiv2/exiv2.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Noms de tests sans accents : voir version_test.cpp.

// Les options de construction d'Exiv2 se lisent dans son exv_conf.h : une
// option qui ne serait plus prise en compte casse la compilation ici.
#ifndef EXV_HAVE_LIBZ
#error "Exiv2 construit sans zlib : les PNG ne seraient pas lus"
#endif
#ifndef EXV_ENABLE_BMFF
#error "Exiv2 construit sans BMFF : ni HEIF ni CR3"
#endif
#ifdef EXV_ENABLE_VIDEO
#error "Exiv2 construit avec son module vidéo, écarté"
#endif
#ifdef EXV_HAVE_XMP_TOOLKIT
#error "Exiv2 construit avec XMP, donc avec expat, écarté"
#endif

namespace {

void put16(std::vector<std::uint8_t>& out, unsigned value)
{
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
}

void put32(std::vector<std::uint8_t>& out, std::uint32_t value)
{
    put16(out, value & 0xFFFF);
    put16(out, value >> 16);
}

// Un JPEG réduit à son bloc APP1 : un TIFF petit-boutiste, un IFD0 qui ne
// contient que le pointeur vers l'IFD EXIF, et celui-ci la seule date de prise
// de vue. Aucune image : Exiv2 n'en a pas besoin pour lire les métadonnées.
std::vector<std::uint8_t> jpeg_with_capture_date(const std::string& date)
{
    constexpr std::uint32_t ifd0 = 8;
    constexpr std::uint32_t exif_ifd = ifd0 + 2 + 12 + 4;
    constexpr std::uint32_t date_offset = exif_ifd + 2 + 12 + 4;

    std::vector<std::uint8_t> tiff{'I', 'I', 0x2A, 0x00};
    put32(tiff, ifd0);

    put16(tiff, 1);
    put16(tiff, 0x8769);  // ExifIFDPointer
    put16(tiff, 4);       // LONG
    put32(tiff, 1);
    put32(tiff, exif_ifd);
    put32(tiff, 0);

    put16(tiff, 1);
    put16(tiff, 0x9003);  // DateTimeOriginal
    put16(tiff, 2);       // ASCII, zéro final compris
    put32(tiff, static_cast<std::uint32_t>(date.size() + 1));
    put32(tiff, date_offset);
    put32(tiff, 0);

    tiff.insert(tiff.end(), date.begin(), date.end());
    tiff.push_back(0);

    std::vector<std::uint8_t> jpeg{0xFF, 0xD8, 0xFF, 0xE1};
    const auto length = static_cast<unsigned>(2 + 6 + tiff.size());
    jpeg.push_back(static_cast<std::uint8_t>(length >> 8));
    jpeg.push_back(static_cast<std::uint8_t>(length & 0xFF));
    jpeg.insert(jpeg.end(), {'E', 'x', 'i', 'f', 0, 0});
    jpeg.insert(jpeg.end(), tiff.begin(), tiff.end());
    jpeg.insert(jpeg.end(), {0xFF, 0xD9});
    return jpeg;
}

}  // namespace

TEST_CASE("Exiv2 lit la date de prise de vue d'un JPEG")
{
    const auto jpeg = jpeg_with_capture_date("2024:07:14 09:30:05");

    auto image = Exiv2::ImageFactory::open(jpeg.data(), jpeg.size());
    REQUIRE(image);
    image->readMetadata();

    const auto& exif = image->exifData();
    const auto date = exif.findKey(Exiv2::ExifKey("Exif.Photo.DateTimeOriginal"));
    REQUIRE(date != exif.end());
    CHECK(date->toString() == "2024:07:14 09:30:05");
}
