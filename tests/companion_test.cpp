#include "core/companion.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Noms de tests sans accents : voir version_test.cpp.

using transfertphoto::attach_companions;
using transfertphoto::companion_kind_from_extension;
using transfertphoto::CompanionKind;

namespace {

// Le résultat du rattachement exprimé en chemins, plus lisible dans un test.
struct Attached {
    std::map<std::string, std::string> owner_of; // accompagnant → porteur
    std::map<std::string, CompanionKind> kind_of;
    std::vector<std::string> orphans;
};

Attached attach(const std::vector<std::string_view>& paths)
{
    const auto result = attach_companions(paths);
    Attached attached;
    for (const auto& link : result.links) {
        const std::string companion(paths[link.companion]);
        // Un accompagnant n'est rattaché qu'une fois.
        REQUIRE(attached.owner_of.count(companion) == 0);
        attached.owner_of[companion] = std::string(paths[link.owner]);
        attached.kind_of[companion] = link.kind;
    }
    for (const auto orphan : result.orphans)
        attached.orphans.emplace_back(paths[orphan]);
    return attached;
}

std::optional<std::string> owner(const Attached& attached, const std::string& companion)
{
    const auto found = attached.owner_of.find(companion);
    if (found == attached.owner_of.end())
        return std::nullopt;
    return found->second;
}

bool is_orphan(const Attached& attached, std::string_view path)
{
    return std::ranges::find(attached.orphans, path) != attached.orphans.end();
}

} // namespace

TEST_CASE("le genre d'accompagnant se lit a l'extension")
{
    CHECK(companion_kind_from_extension("IMG_0001.xmp") == CompanionKind::xmp);
    CHECK(companion_kind_from_extension("DCIM/IMG_0001.CR3.XMP") == CompanionKind::xmp);
    CHECK(companion_kind_from_extension("IMG_1234.AAE") == CompanionKind::aae);
    CHECK(companion_kind_from_extension("GH010123.THM") == CompanionKind::thm);
    CHECK(companion_kind_from_extension("GL010123.LRV") == CompanionKind::lrv);
    CHECK(companion_kind_from_extension("DJI_0001.Srt") == CompanionKind::srt);

    // Le .mov est une vidéo avant tout, et « .xmp » seul n'a pas d'extension.
    CHECK_FALSE(companion_kind_from_extension("IMG_1234.MOV"));
    CHECK_FALSE(companion_kind_from_extension("IMG_0001.JPG"));
    CHECK_FALSE(companion_kind_from_extension(".xmp"));
    CHECK_FALSE(companion_kind_from_extension("xmp/notes.txt"));
}

TEST_CASE("un accompagnant suit le fichier de meme nom dans le meme dossier")
{
    const auto attached = attach({"DCIM/IMG_0001.JPG", "DCIM/img_0001.xmp", "Autre/IMG_0002.JPG", "IMG_0002.xmp",
                                  "Autre/IMG_0003.JPG", "Ailleurs/IMG_0003.xmp"});
    // La casse du nom ne compte pas.
    CHECK(owner(attached, "DCIM/img_0001.xmp") == "DCIM/IMG_0001.JPG");
    // Un autre dossier, même la racine, n'est pas le même dossier.
    CHECK(is_orphan(attached, "IMG_0002.xmp"));
    CHECK(is_orphan(attached, "Ailleurs/IMG_0003.xmp"));
    CHECK(attached.orphans.size() == 2);
}

TEST_CASE("le xmp suit le nom entier ou le nom sans extension")
{
    const auto attached = attach({"IMG_0001.CR3", "IMG_0001.JPG", "IMG_0001.CR3.xmp", "IMG_0001.JPG.xmp",
                                  "IMG_0002.JPG", "IMG_0002.xmp", "IMG_0003.NEF", "IMG_0003.JPG", "IMG_0003.xmp"});
    // À la darktable, le nom entier désigne le fichier, même dans une paire.
    CHECK(owner(attached, "IMG_0001.CR3.xmp") == "IMG_0001.CR3");
    CHECK(owner(attached, "IMG_0001.JPG.xmp") == "IMG_0001.JPG");
    // À la Lightroom, le nom sans extension, et le RAW d'abord.
    CHECK(owner(attached, "IMG_0002.xmp") == "IMG_0002.JPG");
    CHECK(owner(attached, "IMG_0003.xmp") == "IMG_0003.NEF");
    CHECK(attached.orphans.empty());
}

TEST_CASE("le choix entre deux candidats ne depend pas de l'ordre du parcours")
{
    const auto forward = attach({"IMG_0001.JPG", "IMG_0001.jpeg", "IMG_0001.xmp"});
    const auto backward = attach({"IMG_0001.xmp", "IMG_0001.jpeg", "IMG_0001.JPG"});
    CHECK(owner(forward, "IMG_0001.xmp") == "IMG_0001.JPG");
    CHECK(owner(backward, "IMG_0001.xmp") == "IMG_0001.JPG");
}

TEST_CASE("les fichiers d'une photo d'iPhone retouchee")
{
    const auto attached = attach({"DCIM/100APPLE/IMG_1234.HEIC", "DCIM/100APPLE/IMG_E1234.HEIC",
                                  "DCIM/100APPLE/IMG_1234.AAE", "DCIM/100APPLE/IMG_O1234.AAE"});
    // Les deux .aae vont à l'originale, sous l'une ou l'autre forme de nom.
    CHECK(owner(attached, "DCIM/100APPLE/IMG_1234.AAE") == "DCIM/100APPLE/IMG_1234.HEIC");
    CHECK(owner(attached, "DCIM/100APPLE/IMG_O1234.AAE") == "DCIM/100APPLE/IMG_1234.HEIC");
    // La retouchée est une photo à part entière, rattachée à rien.
    CHECK_FALSE(owner(attached, "DCIM/100APPLE/IMG_E1234.HEIC"));
    CHECK(attached.owner_of.size() == 2);
    CHECK(attached.orphans.empty());
}

TEST_CASE("un aae sans photo suit la video de meme nom")
{
    const auto attached = attach({"IMG_1240.MOV", "IMG_1240.AAE", "IMG_O1241.AAE", "IMG_O12X.AAE", "IMG_O.AAE"});
    CHECK(owner(attached, "IMG_1240.AAE") == "IMG_1240.MOV");
    CHECK(is_orphan(attached, "IMG_O1241.AAE"));
    CHECK(is_orphan(attached, "IMG_O12X.AAE"));
    CHECK(is_orphan(attached, "IMG_O.AAE"));
}

TEST_CASE("le mov d'une photo animee suit sa photo")
{
    const auto attached = attach({"IMG_1234.HEIC", "IMG_1234.MOV", "IMG_1234.AAE", "IMG_1235.MOV", "IMG_1236.JPG",
                                  "IMG_1236.MP4", "MVI_0001.MOV", "MVI_0001.THM"});
    CHECK(owner(attached, "IMG_1234.MOV") == "IMG_1234.HEIC");
    CHECK(attached.kind_of.at("IMG_1234.MOV") == CompanionKind::live_video);
    CHECK(owner(attached, "IMG_1234.AAE") == "IMG_1234.HEIC");
    // Sans photo de même nom, un .mov est une vidéo, pas un orphelin ; un .mp4
    // n'est jamais une photo animée.
    CHECK_FALSE(owner(attached, "IMG_1235.MOV"));
    CHECK_FALSE(owner(attached, "IMG_1236.MP4"));
    // Une vidéo ordinaire porte ses propres accompagnants.
    CHECK(owner(attached, "MVI_0001.THM") == "MVI_0001.MOV");
    CHECK(attached.orphans.empty());
}

TEST_CASE("la video d'une photo animee ne porte rien elle-meme")
{
    const auto attached = attach({"IMG_1234.JPG", "IMG_1234.MOV", "IMG_1234.THM"});
    CHECK(owner(attached, "IMG_1234.MOV") == "IMG_1234.JPG");
    CHECK(is_orphan(attached, "IMG_1234.THM"));
}

TEST_CASE("les fichiers d'une GoPro")
{
    const auto attached = attach({"DCIM/100GOPRO/GH010123.MP4", "DCIM/100GOPRO/GH010123.THM",
                                  "DCIM/100GOPRO/GL010123.LRV", "DCIM/100GOPRO/GX010124.MP4",
                                  "DCIM/100GOPRO/GL010124.LRV", "DCIM/100GOPRO/GOPR0125.MP4",
                                  "DCIM/100GOPRO/GOPR0125.LRV", "DCIM/100GOPRO/GOPR0125.THM",
                                  "DCIM/100GOPRO/GL010126.LRV"});
    CHECK(owner(attached, "DCIM/100GOPRO/GH010123.THM") == "DCIM/100GOPRO/GH010123.MP4");
    CHECK(owner(attached, "DCIM/100GOPRO/GL010123.LRV") == "DCIM/100GOPRO/GH010123.MP4");
    CHECK(owner(attached, "DCIM/100GOPRO/GL010124.LRV") == "DCIM/100GOPRO/GX010124.MP4");
    CHECK(owner(attached, "DCIM/100GOPRO/GOPR0125.LRV") == "DCIM/100GOPRO/GOPR0125.MP4");
    CHECK(owner(attached, "DCIM/100GOPRO/GOPR0125.THM") == "DCIM/100GOPRO/GOPR0125.MP4");
    CHECK(is_orphan(attached, "DCIM/100GOPRO/GL010126.LRV"));
    CHECK(attached.orphans.size() == 1);
}

TEST_CASE("le srt d'un drone DJI suit sa video et jamais une photo")
{
    const auto attached = attach({"DJI_0001.MP4", "DJI_0001.SRT", "DJI_0002.SRT", "DJI_0003.JPG", "DJI_0003.SRT"});
    CHECK(owner(attached, "DJI_0001.SRT") == "DJI_0001.MP4");
    CHECK(is_orphan(attached, "DJI_0002.SRT"));
    CHECK(is_orphan(attached, "DJI_0003.SRT"));
}

TEST_CASE("un fichier qui n'est ni media ni accompagnant n'apparait pas")
{
    const auto result = attach_companions(std::vector<std::string_view>{"IMG_0001.JPG", "notes.txt", "IMG_0001.txt"});
    CHECK(result.links.empty());
    CHECK(result.orphans.empty());
}
