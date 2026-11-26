#include "core/photo_date.hpp"

#include "core/source_io.hpp"

#include <exiv2/exiv2.hpp>

#include <cstddef>
#include <exception>
#include <memory>
#include <mutex>
#include <span>
#include <string>

namespace transfertphoto {
namespace {

// Exiv2 écrit ses avertissements sur la sortie d'erreur, un par fichier abîmé.
// Ils n'ont rien à y faire : ce que je retiens d'un fichier, je le rends.
void silence_exiv2()
{
    static std::once_flag once;
    std::call_once(once, [] { Exiv2::LogMsg::setLevel(Exiv2::LogMsg::mute); });
}

std::optional<std::string> ascii_value(const Exiv2::ExifData& exif, const char* key)
{
    const auto found = exif.findKey(Exiv2::ExifKey(key));
    if (found == exif.end())
        return std::nullopt;
    return found->toString();
}

std::optional<LocalDateTime> date_with_subsecond(const Exiv2::ExifData& exif, const char* date_key,
                                                 const char* subsecond_key)
{
    const auto text = ascii_value(exif, date_key);
    if (!text)
        return std::nullopt;
    auto date = parse_exif_date_time(*text);
    if (!date)
        return std::nullopt;
    if (const auto subsecond_text = ascii_value(exif, subsecond_key)) {
        if (const auto subsecond = parse_exif_subsecond(*subsecond_text))
            date->nanosecond = *subsecond;
    }
    return date;
}

} // namespace

PhotoDateRead read_photo_date(Source& source, std::string_view relative_path, std::uint64_t size)
{
    silence_exiv2();

    PhotoDateRead result;
    try {
        // L'image garde l'adaptateur ; un type que rien ne reconnaît donne
        // une image nulle.
        const auto image = Exiv2::ImageFactory::open(
            std::make_unique<SourceIo>(source, std::string(relative_path), size));
        if (image) {
            image->readMetadata();
            const auto& exif = image->exifData();
            result.date = date_with_subsecond(exif, "Exif.Photo.DateTimeOriginal",
                                              "Exif.Photo.SubSecTimeOriginal");
            if (!result.date)
                result.date = date_with_subsecond(exif, "Exif.Photo.DateTimeDigitized",
                                                  "Exif.Photo.SubSecTimeDigitized");
        }
    } catch (const std::exception&) {
        // Exiv2::Error le plus souvent, mais un fichier abîmé peut aussi lui
        // faire demander une allocation démesurée. Dans les deux cas, pas de
        // date.
        result.date.reset();
    }

    // Sans date, dire si c'est la source qui a lâché : l'adaptateur a disparu
    // avec l'image ou l'exception, je repose la question à la source.
    if (!result.date) {
        std::byte probe{};
        (void)source.read_range(relative_path, 0, std::span(&probe, 1), result.source_error);
    }
    return result;
}

} // namespace transfertphoto
