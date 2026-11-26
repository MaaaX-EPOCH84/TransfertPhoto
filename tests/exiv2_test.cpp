#include "exif_samples.hpp"

#include <exiv2/exiv2.hpp>

#include <catch2/catch_test_macros.hpp>

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

TEST_CASE("Exiv2 lit la date de prise de vue d'un JPEG")
{
    const auto jpeg = test_support::jpeg_with_capture_date("2024:07:14 09:30:05");

    auto image = Exiv2::ImageFactory::open(jpeg.data(), jpeg.size());
    REQUIRE(image);
    image->readMetadata();

    const auto& exif = image->exifData();
    const auto date = exif.findKey(Exiv2::ExifKey("Exif.Photo.DateTimeOriginal"));
    REQUIRE(date != exif.end());
    CHECK(date->toString() == "2024:07:14 09:30:05");
}
