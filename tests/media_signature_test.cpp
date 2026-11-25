#include "core/media_signature.hpp"

#include "core/source.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Noms de tests sans accents : voir version_test.cpp.
//
// Attention aux échappements dans les chaînes : « \x18f » serait lu comme un
// seul octet 0x18F. Je coupe la chaîne après chaque \x suivi d'un chiffre
// hexadécimal.

using namespace std::string_view_literals;
using transfertphoto::check_signature;
using transfertphoto::format_from_signature;
using transfertphoto::media_extensions;
using transfertphoto::MediaFormat;
using transfertphoto::signature_head_size;
using transfertphoto::SignatureVerdict;

namespace {

std::vector<std::byte> bytes(std::string_view text)
{
    std::vector<std::byte> result(text.size());
    std::ranges::transform(text, result.begin(), [](char c) { return static_cast<std::byte>(c); });
    return result;
}

// Paquets MPEG-TS : l'octet 0x47 en tête de chacun, précédé de 4 octets
// d'horodatage pour les paquets de 192 octets.
std::vector<std::byte> mpeg_ts(std::size_t packet_size, int packets)
{
    const std::size_t prefix = packet_size == 192 ? 4 : 0;
    std::vector<std::byte> result(packet_size * static_cast<std::size_t>(packets), std::byte{0xFF});
    for (int i = 0; i < packets; ++i)
        result[static_cast<std::size_t>(i) * packet_size + prefix] = std::byte{0x47};
    return result;
}

// Un début de fichier typique pour chaque format de la liste.
std::vector<std::byte> sample(MediaFormat format)
{
    switch (format) {
    case MediaFormat::jpeg:
        return bytes("\xFF\xD8\xFF\xE0\0\x10JFIF\0"sv);
    case MediaFormat::png:
        return bytes("\x89PNG\r\n\x1A\n\0\0\0\x0DIHDR"sv);
    case MediaFormat::heif:
        return bytes("\0\0\0\x18" "ftypheic\0\0\0\0mif1heic"sv);
    case MediaFormat::tiff:
    case MediaFormat::dng:
    case MediaFormat::arw:
        return bytes("II*\0\x08\0\0\0"sv);
    case MediaFormat::nef:
        return bytes("MM\0*\0\0\0\x08"sv);
    case MediaFormat::webp:
        return bytes("RIFF\x24\0\0\0WEBPVP8 "sv);
    case MediaFormat::gif:
        return bytes("GIF89a\x01\0\x01\0"sv);
    case MediaFormat::cr2:
        return bytes("II*\0\x10\0\0\0CR\x02\0"sv);
    case MediaFormat::cr3:
        return bytes("\0\0\0\x18" "ftypcrx \0\0\0\x01" "crx isom"sv);
    case MediaFormat::raf:
        return bytes("FUJIFILMCCD-RAW 0201FF383501"sv);
    case MediaFormat::orf:
        return bytes("IIRO\x08\0\0\0"sv);
    case MediaFormat::rw2:
        return bytes("IIU\0\x18\0\0\0"sv);
    case MediaFormat::mp4:
        return bytes("\0\0\0\x20" "ftypisom\0\0\x02\0isomiso2avc1mp41"sv);
    case MediaFormat::mov:
        return bytes("\0\0\0\x14" "ftypqt  \0\0\0\0qt  "sv);
    case MediaFormat::m4v:
        return bytes("\0\0\0\x1C" "ftypM4V \0\0\0\x01M4V M4A mp42isom"sv);
    case MediaFormat::avi:
        return bytes("RIFF\0\0\0\0AVI LIST"sv);
    case MediaFormat::mkv:
        return bytes("\x1A\x45\xDF\xA3\x01\0\0\0"sv);
    case MediaFormat::mpeg_ts:
        return mpeg_ts(192, 3);
    case MediaFormat::three_gp:
        return bytes("\0\0\0\x14" "ftyp3gp4\0\0\0\0isom"sv);
    }
    return {};
}

// Familles de contenus que la signature ne doit jamais confondre ; à
// l'intérieur des familles TIFF et vidéo ISO, les cas sont testés un par un.
int family(MediaFormat format)
{
    switch (format) {
    case MediaFormat::tiff:
    case MediaFormat::dng:
    case MediaFormat::cr2:
    case MediaFormat::nef:
    case MediaFormat::arw:
    case MediaFormat::orf:
    case MediaFormat::rw2:
        return 1;
    case MediaFormat::mp4:
    case MediaFormat::mov:
    case MediaFormat::m4v:
    case MediaFormat::three_gp:
        return 2;
    default:
        return 100 + static_cast<int>(format);
    }
}

SignatureVerdict verdict(MediaFormat expected, std::string_view head)
{
    return check_signature(expected, bytes(head)).verdict;
}

// Source qui rend un contenu fixé, ou une erreur, et retient ce qu'on lui a demandé.
class FakeSource final : public transfertphoto::Source {
public:
    explicit FakeSource(std::vector<std::byte> content, std::error_code error = {})
        : content_(std::move(content)), error_(error)
    {
    }

    std::string display_name() const override { return "essai"; }
    bool is_available() const override { return true; }
    transfertphoto::ScanResult scan(const std::function<void(const transfertphoto::SourceEntry&)>&,
                                    std::stop_token) override
    {
        return {};
    }
    std::size_t read_head(std::string_view relative_path, std::span<std::byte> buffer,
                          std::error_code& error) override
    {
        requested_path = std::string(relative_path);
        requested_size = buffer.size();
        error = error_;
        if (error)
            return 0;
        const auto count = std::min(buffer.size(), content_.size());
        std::copy_n(content_.begin(), count, buffer.begin());
        return count;
    }

    std::string requested_path;
    std::size_t requested_size = 0;

private:
    std::vector<std::byte> content_;
    std::error_code error_;
};

} // namespace

TEST_CASE("chaque extension accepte la signature de son format")
{
    for (const auto& entry : media_extensions()) {
        INFO("extension " << entry.extension);
        const auto check = check_signature(entry.format, sample(entry.format));
        CHECK(check.verdict == SignatureVerdict::matches);
        CHECK_FALSE(check.error);
    }
}

TEST_CASE("deux familles differentes ne se confondent jamais")
{
    for (const auto& expected : media_extensions()) {
        for (const auto& content : media_extensions()) {
            if (family(expected.format) == family(content.format))
                continue;
            INFO("extension " << expected.extension << ", contenu de " << content.extension);
            CHECK(check_signature(expected.format, sample(content.format)).verdict
                  == SignatureVerdict::mismatch);
        }
    }
}

TEST_CASE("un desaccord dit ce que le contenu semble etre")
{
    const auto jpeg_as_png = check_signature(MediaFormat::png, sample(MediaFormat::jpeg));
    CHECK(jpeg_as_png.verdict == SignatureVerdict::mismatch);
    CHECK(jpeg_as_png.detected == MediaFormat::jpeg);

    const auto heic_as_jpeg = check_signature(MediaFormat::jpeg, sample(MediaFormat::heif));
    CHECK(heic_as_jpeg.verdict == SignatureVerdict::mismatch);
    CHECK(heic_as_jpeg.detected == MediaFormat::heif);

    const auto webp_as_avi = check_signature(MediaFormat::avi, sample(MediaFormat::webp));
    CHECK(webp_as_avi.verdict == SignatureVerdict::mismatch);
    CHECK(webp_as_avi.detected == MediaFormat::webp);
}

TEST_CASE("un fichier AppleDouble de macOS est un desaccord")
{
    // Ce que macOS laisse à côté de IMG_0001.JPG sur une carte en exFAT, sous
    // le nom ._IMG_0001.JPG.
    const auto check = check_signature(MediaFormat::jpeg, bytes("\0\x05\x16\x07\0\x02\0\0Mac OS X        "sv));
    CHECK(check.verdict == SignatureVerdict::mismatch);
    CHECK_FALSE(check.detected);
}

TEST_CASE("un fichier vide ou tronque est un desaccord")
{
    CHECK(verdict(MediaFormat::jpeg, ""sv) == SignatureVerdict::mismatch);
    CHECK(verdict(MediaFormat::jpeg, "\xFF\xD8"sv) == SignatureVerdict::mismatch);
    CHECK(verdict(MediaFormat::png, "\x89PNG"sv) == SignatureVerdict::mismatch);
    // Boîte ftyp coupée avant sa marque.
    CHECK(verdict(MediaFormat::mp4, "\0\0\0\x18" "ftyp"sv) == SignatureVerdict::mismatch);
    CHECK(verdict(MediaFormat::cr2, "II*\0\x10\0\0\0C"sv) == SignatureVerdict::mismatch);
}

TEST_CASE("les RAW sur base TIFF")
{
    // DNG, NEF et ARW n'ont pas de signature propre : un TIFF dans les deux
    // ordres d'octets leur convient.
    for (const auto format : {MediaFormat::dng, MediaFormat::nef, MediaFormat::arw, MediaFormat::tiff}) {
        CHECK(verdict(format, "II*\0\x08\0\0\0"sv) == SignatureVerdict::matches);
        CHECK(verdict(format, "MM\0*\0\0\0\x08"sv) == SignatureVerdict::matches);
    }
    // Le BigTIFF aussi.
    CHECK(verdict(MediaFormat::tiff, "II+\0\x08\0\0\0"sv) == SignatureVerdict::matches);
    CHECK(verdict(MediaFormat::tiff, "MM\0+\0\x08\0\0"sv) == SignatureVerdict::matches);

    // Un CR2 est un TIFF, mais un TIFF n'est pas un CR2, et un CR2 renommé en
    // NEF est un désaccord.
    CHECK(verdict(MediaFormat::tiff, "II*\0\x10\0\0\0CR\x02\0"sv) == SignatureVerdict::matches);
    CHECK(verdict(MediaFormat::cr2, "II*\0\x08\0\0\0"sv) == SignatureVerdict::mismatch);
    CHECK(verdict(MediaFormat::nef, "II*\0\x10\0\0\0CR\x02\0"sv) == SignatureVerdict::mismatch);

    // ORF et RW2 acceptent leur nombre magique propre ou un TIFF ordinaire,
    // mais ce nombre n'est pas un TIFF pour les autres.
    CHECK(verdict(MediaFormat::orf, "IIRS\x08\0\0\0"sv) == SignatureVerdict::matches);
    CHECK(verdict(MediaFormat::orf, "MMOR\0\0\0\x08"sv) == SignatureVerdict::matches);
    CHECK(verdict(MediaFormat::orf, "II*\0\x08\0\0\0"sv) == SignatureVerdict::matches);
    CHECK(verdict(MediaFormat::rw2, "II*\0\x08\0\0\0"sv) == SignatureVerdict::matches);
    CHECK(verdict(MediaFormat::dng, "IIRO\x08\0\0\0"sv) == SignatureVerdict::mismatch);
    CHECK(verdict(MediaFormat::tiff, "IIU\0\x18\0\0\0"sv) == SignatureVerdict::mismatch);
    CHECK(verdict(MediaFormat::rw2, "IIRO\x08\0\0\0"sv) == SignatureVerdict::mismatch);
}

TEST_CASE("les marques de la boite ftyp separent HEIF, CR3 et video")
{
    // HEIF par sa marque générique.
    CHECK(verdict(MediaFormat::heif, "\0\0\0\x18" "ftypmif1\0\0\0\0mif1heic"sv) == SignatureVerdict::matches);
    CHECK(verdict(MediaFormat::heif, "\0\0\0\x18" "ftypmsf1\0\0\0\0msf1hevc"sv) == SignatureVerdict::matches);
    // Marque des HEIF 10 bits, celle que je crois être écrite par les boîtiers Canon.
    CHECK(verdict(MediaFormat::heif, "\0\0\0\x18" "ftypheix\0\0\0\0mif1heix"sv) == SignatureVerdict::matches);

    // Une photo HEIC renommée en vidéo, et l'inverse.
    const auto heic_as_mp4 = check_signature(MediaFormat::mp4, sample(MediaFormat::heif));
    CHECK(heic_as_mp4.verdict == SignatureVerdict::mismatch);
    CHECK(heic_as_mp4.detected == MediaFormat::heif);
    CHECK(verdict(MediaFormat::heif, "\0\0\0\x20" "ftypisom\0\0\x02\0isomiso2avc1mp41"sv)
          == SignatureVerdict::mismatch);

    // Les vidéos ISO s'acceptent entre elles, quelle que soit leur marque.
    for (const auto format : {MediaFormat::mp4, MediaFormat::mov, MediaFormat::m4v, MediaFormat::three_gp}) {
        for (const auto content : {MediaFormat::mp4, MediaFormat::mov, MediaFormat::m4v, MediaFormat::three_gp})
            CHECK(check_signature(format, sample(content)).verdict == SignatureVerdict::matches);
    }
}

TEST_CASE("un vieux QuickTime sans boite ftyp est une video")
{
    const auto moov = bytes("\0\0\x01\0moov\0\0\0\x6Cmvhd"sv);
    const auto wide = bytes("\0\0\0\x08wide\0\x10\0\0mdat"sv);
    for (const auto format : {MediaFormat::mov, MediaFormat::mp4}) {
        CHECK(check_signature(format, moov).verdict == SignatureVerdict::matches);
        CHECK(check_signature(format, wide).verdict == SignatureVerdict::matches);
    }
    CHECK(check_signature(MediaFormat::mov, moov).detected == MediaFormat::mov);
    CHECK(check_signature(MediaFormat::heif, moov).verdict == SignatureVerdict::mismatch);
}

TEST_CASE("un flux MPEG-TS se reconnait a la repetition de ses paquets")
{
    static_assert(4 + 2 * 192 < signature_head_size, "le tampon doit couvrir trois paquets de 192 octets");

    CHECK(check_signature(MediaFormat::mpeg_ts, mpeg_ts(188, 3)).verdict == SignatureVerdict::matches);
    CHECK(check_signature(MediaFormat::mpeg_ts, mpeg_ts(192, 3)).verdict == SignatureVerdict::matches);

    // Deux paquets ne suffisent pas, un « G » isolé encore moins.
    CHECK(check_signature(MediaFormat::mpeg_ts, mpeg_ts(188, 2)).verdict == SignatureVerdict::mismatch);
    CHECK(verdict(MediaFormat::mpeg_ts, "G\0\0\0\0\0\0\0"sv) == SignatureVerdict::mismatch);

    // Paquets de 192 octets dont le troisième a perdu son octet de synchronisation.
    auto broken = mpeg_ts(192, 3);
    broken[4 + 2 * 192] = std::byte{0};
    CHECK(check_signature(MediaFormat::mpeg_ts, broken).verdict == SignatureVerdict::mismatch);
}

TEST_CASE("un RIFF qui n'est ni WebP ni AVI n'est rien")
{
    const auto wave = bytes("RIFF\x24\0\0\0WAVEfmt "sv);
    CHECK_FALSE(format_from_signature(wave));
    CHECK(check_signature(MediaFormat::webp, wave).verdict == SignatureVerdict::mismatch);
    CHECK(check_signature(MediaFormat::avi, wave).verdict == SignatureVerdict::mismatch);
}

TEST_CASE("la signature est lue dans la source")
{
    FakeSource source(sample(MediaFormat::cr3));
    const auto check = check_signature(source, "DCIM/100CANON/IMG_0001.CR3", MediaFormat::cr3);
    CHECK(check.verdict == SignatureVerdict::matches);
    CHECK(check.detected == MediaFormat::cr3);
    CHECK(source.requested_path == "DCIM/100CANON/IMG_0001.CR3");
    CHECK(source.requested_size == signature_head_size);

    // Seuls les octets réellement lus comptent : un fichier « IIU » n'est pas
    // complété par le zéro qui suit dans le tampon, ce qui en ferait un RW2.
    FakeSource truncated(bytes("IIU"sv));
    CHECK(check_signature(truncated, "court.rw2", MediaFormat::rw2).verdict == SignatureVerdict::mismatch);
}

TEST_CASE("un fichier qu'on ne peut pas lire est illisible")
{
    FakeSource source({}, std::make_error_code(std::errc::io_error));
    const auto check = check_signature(source, "IMG_0001.JPG", MediaFormat::jpeg);
    CHECK(check.verdict == SignatureVerdict::unreadable);
    CHECK(check.error == std::make_error_code(std::errc::io_error));
    CHECK_FALSE(check.detected);
}
