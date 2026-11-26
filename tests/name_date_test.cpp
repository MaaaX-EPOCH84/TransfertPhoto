#include "core/name_date.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

// Noms de tests sans accents : voir version_test.cpp.

using transfertphoto::date_from_file_name;
using transfertphoto::LocalDateTime;

namespace {

// La date trouvée, écrite pour comparer d'un coup ; « - » s'il n'y en a pas,
// l'heure omise quand le nom ne la donne pas.
std::string found(std::string_view path)
{
    const auto date = date_from_file_name(path);
    if (!date)
        return "-";
    auto text = to_string(date->value);
    if (!date->has_time)
        text = text.substr(0, 10);
    if (date->value.nanosecond != 0)
        text += "." + std::to_string(date->value.nanosecond / 1'000'000);
    return text;
}

} // namespace

TEST_CASE("les motifs des telephones et des appareils sont reconnus")
{
    // Android, Samsung
    CHECK(found("DCIM/Camera/IMG_20231225_180400.jpg") == "2023-12-25 18:04:00");
    CHECK(found("DCIM/Camera/VID_20231225_180400.mp4") == "2023-12-25 18:04:00");
    CHECK(found("DCIM/Camera/20231225_180400.jpg") == "2023-12-25 18:04:00");
    CHECK(found("20231225_180400(1).jpg") == "2023-12-25 18:04:00");
    // Pixel, avec les millisecondes
    CHECK(found("PXL_20231225_170400123.jpg") == "2023-12-25 17:04:00.123");
    CHECK(found("PXL_20231225_170400123.MP.jpg") == "2023-12-25 17:04:00.123");
    // Date et heure collées
    CHECK(found("IMG20231225180400.jpg") == "2023-12-25 18:04:00");
    // WhatsApp : le jour seul
    CHECK(found("WhatsApp Images/IMG-20231225-WA0001.jpg") == "2023-12-25");
    CHECK(found("VID-20231225-WA0003.mp4") == "2023-12-25");
}

TEST_CASE("les motifs des logiciels et des captures d'ecran sont reconnus")
{
    // Dropbox, chargement de l'appareil photo
    CHECK(found("Camera Uploads/2023-12-25 18.04.00.jpg") == "2023-12-25 18:04:00");
    // Android
    CHECK(found("Screenshot_2023-12-25-18-04-00.png") == "2023-12-25 18:04:00");
    CHECK(found("Screenshot_20231225-180400.png") == "2023-12-25 18:04:00");
    // Samsung, avec le nom de l'application
    CHECK(found("Screenshot_20231225_180400_Chrome.jpg") == "2023-12-25 18:04:00");
    // macOS, en anglais et en français
    CHECK(found("Screenshot 2023-12-25 at 18.04.00.png") == "2023-12-25 18:04:00");
    CHECK(found("Capture d’écran 2023-12-25 à 18.04.00.png") == "2023-12-25 18:04:00");
    // Windows
    CHECK(found("Screenshot 2023-12-25 180400.png") == "2023-12-25 18:04:00");
    // ISO 8601
    CHECK(found("2023-12-25T18:04:00.jpg") == "2023-12-25 18:04:00");
    // La date seule, avec des points ou des soulignés
    CHECK(found("Scans/1985.07.14 Mariage.jpg") == "1985-07-14");
    CHECK(found("1985_07_14.tif") == "1985-07-14");
}

TEST_CASE("seul le nom compte, ni les dossiers ni l'extension")
{
    CHECK(found("2023-12-25/IMG_0001.jpg") == "-");
    CHECK(found("20231225/DSC_0001.JPG") == "-");
    CHECK(found("clip.3gp") == "-");
}

TEST_CASE("une heure impossible laisse le jour seul")
{
    CHECK(found("IMG_20231225_250000.jpg") == "2023-12-25");
    CHECK(found("2023-12-25 18.61.00.jpg") == "2023-12-25");
    // Trop loin de la date pour en être l'heure.
    CHECK(found("2023-12-25 photo 180400.jpg") == "2023-12-25");
}

TEST_CASE("les faux positifs sont ecartes")
{
    // Numéros qui ne sont pas des dates
    CHECK(found("DSC_12345678.JPG") == "-");
    CHECK(found("IMG_99999999.jpg") == "-");
    CHECK(found("P1000123.JPG") == "-");
    CHECK(found("DSC01234.JPG") == "-");
    // Dates qui n'existent pas
    CHECK(found("IMG_20230230_120000.jpg") == "-");
    CHECK(found("IMG_20231301_120000.jpg") == "-");
    CHECK(found("2023-02-29.jpg") == "-");
    // Années hors bornes
    CHECK(found("IMG_18991231_120000.jpg") == "-");
    CHECK(found("IMG_21000101_120000.jpg") == "-");
    // Chiffres collés à d'autres
    CHECK(found("IMG_120231225_180400.jpg") == "-");
    CHECK(found("IMG_202312251.jpg") == "-");
    CHECK(found("12023-12-25.jpg") == "-");
    CHECK(found("2023-12-251.jpg") == "-");
    // Séparateurs mêlés
    CHECK(found("2023-12_25.jpg") == "-");
}

TEST_CASE("la premiere date valide l'emporte")
{
    CHECK(found("20230230_puis_20231225.jpg") == "2023-12-25");
    CHECK(found("IMG_20231225_180400 copie 20240101.jpg") == "2023-12-25 18:04:00");
}

TEST_CASE("le 29 fevrier d'une annee bissextile passe")
{
    CHECK(found("IMG_20240229_000000.jpg") == "2024-02-29 00:00:00");
}
