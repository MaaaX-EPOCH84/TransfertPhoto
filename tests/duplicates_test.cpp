#include "core/duplicates.hpp"

#include "memory_source.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <stop_token>
#include <string>
#include <vector>

// Noms de tests sans accents : voir version_test.cpp.

using test_support::MemorySource;
using transfertphoto::duplicate_head_bytes;
using transfertphoto::DuplicateProgress;
using transfertphoto::DuplicateSearch;
using transfertphoto::FileRef;
using transfertphoto::find_duplicates;
using transfertphoto::Inventory;
using transfertphoto::MediaItem;
using transfertphoto::ScanOutcome;
using transfertphoto::Side;

namespace {

using Bytes = std::vector<std::uint8_t>;

Bytes content(std::size_t size, std::uint8_t seed)
{
    Bytes bytes(size);
    for (std::size_t i = 0; i < size; ++i)
        bytes[i] = static_cast<std::uint8_t>((i * 7 + seed) % 251);
    return bytes;
}

// Une source et une destination, avec leurs inventaires.
struct Fixture {
    MemorySource source;
    MemorySource destination;
    Inventory source_inventory;
    Inventory destination_inventory;

    void add(Side side, const std::string& path, const Bytes& bytes)
    {
        auto& memory = side == Side::source ? source : destination;
        auto& inventory = side == Side::source ? source_inventory : destination_inventory;
        memory.contents[path] = bytes;
        MediaItem item;
        item.entry.relative_path = path;
        item.entry.size = bytes.size();
        inventory.media.push_back(item);
    }

    DuplicateSearch find(std::stop_token stop = {},
                         const std::function<void(const DuplicateProgress&)>& on_progress = {})
    {
        return find_duplicates(source, source_inventory, destination, destination_inventory, stop, on_progress);
    }

    std::uint64_t bytes_read() const { return source.bytes_read + destination.bytes_read; }
};

constexpr std::size_t big = 200 * 1024; // plus grand que le début examiné

} // namespace

TEST_CASE("des tailles differentes ne se lisent meme pas")
{
    Fixture fixture;
    fixture.add(Side::source, "a.jpg", content(1000, 1));
    fixture.add(Side::source, "b.jpg", content(1001, 1));
    fixture.add(Side::destination, "c.jpg", content(1002, 1));
    const auto search = fixture.find();
    CHECK(search.outcome == ScanOutcome::completed);
    CHECK(search.groups.empty());
    CHECK(fixture.bytes_read() == 0);
}

TEST_CASE("meme taille, debut different : seuls les debuts sont lus")
{
    Fixture fixture;
    fixture.add(Side::source, "a.mp4", content(big, 1));
    fixture.add(Side::destination, "b.mp4", content(big, 2));
    const auto search = fixture.find();
    CHECK(search.groups.empty());
    CHECK(fixture.bytes_read() == 2 * duplicate_head_bytes);
}

TEST_CASE("meme debut, fin differente : l'empreinte partielle ne conclut pas seule")
{
    Fixture fixture;
    auto other = content(big, 1);
    other.back() ^= 0xFF;
    fixture.add(Side::source, "a.mp4", content(big, 1));
    fixture.add(Side::destination, "b.mp4", other);
    const auto search = fixture.find();
    CHECK(search.groups.empty());
    CHECK(fixture.bytes_read() == 2 * duplicate_head_bytes + 2 * big);
}

TEST_CASE("trois copies forment un groupe, la source d'abord")
{
    Fixture fixture;
    fixture.add(Side::source, "DCIM/b.mp4", content(big, 5));
    fixture.add(Side::source, "DCIM/a.mp4", content(big, 5));
    fixture.add(Side::destination, "2023/12/25/a.mp4", content(big, 5));
    fixture.add(Side::destination, "2023/12/25/autre.mp4", content(big, 6));

    const auto search = fixture.find();
    REQUIRE(search.groups.size() == 1);
    const auto& group = search.groups[0];
    CHECK(group.size == big);
    CHECK(group.members
          == std::vector<FileRef>{{Side::source, 1}, {Side::source, 0}, {Side::destination, 0}});
}

TEST_CASE("un petit fichier n'est lu qu'une fois")
{
    Fixture fixture;
    fixture.add(Side::source, "a.jpg", content(5000, 3));
    fixture.add(Side::source, "b.jpg", content(5000, 3));
    const auto search = fixture.find();
    REQUIRE(search.groups.size() == 1);
    CHECK(fixture.bytes_read() == 2 * 5000);
    CHECK(fixture.source.reads["a.jpg"] == 1);
}

TEST_CASE("un fichier vide n'est le doublon de personne")
{
    Fixture fixture;
    fixture.add(Side::source, "a.jpg", {});
    fixture.add(Side::source, "b.jpg", {});
    fixture.add(Side::destination, "c.jpg", {});
    CHECK(fixture.find().groups.empty());
}

TEST_CASE("les groupes viennent dans l'ordre de leur premier membre")
{
    Fixture fixture;
    fixture.add(Side::destination, "z/1.jpg", content(3000, 1));
    fixture.add(Side::destination, "z/2.jpg", content(3000, 1));
    fixture.add(Side::source, "b.jpg", content(4000, 2));
    fixture.add(Side::destination, "b.jpg", content(4000, 2));
    fixture.add(Side::source, "a.jpg", content(5000, 3));
    fixture.add(Side::source, "c.jpg", content(5000, 3));

    const auto search = fixture.find();
    REQUIRE(search.groups.size() == 3);
    // Rangs dans l'inventaire de la source : b.jpg 0, a.jpg 1, c.jpg 2.
    CHECK(search.groups[0].members.front() == FileRef{Side::source, 1});      // a.jpg
    CHECK(search.groups[1].members.front() == FileRef{Side::source, 0});      // b.jpg
    CHECK(search.groups[2].members.front() == FileRef{Side::destination, 0}); // z/1.jpg
}

TEST_CASE("un fichier illisible ou change sort de la recherche et se note")
{
    Fixture fixture;
    fixture.add(Side::source, "a.jpg", content(5000, 3));
    fixture.add(Side::source, "b.jpg", content(5000, 3));
    fixture.add(Side::destination, "c.jpg", content(5000, 3));
    fixture.add(Side::destination, "d.jpg", content(5000, 3));
    fixture.source.failing.insert("b.jpg");
    // Annoncé à 5000 octets, il n'en a plus que 10.
    fixture.destination.contents["d.jpg"].resize(10);

    const auto search = fixture.find();
    CHECK(search.outcome == ScanOutcome::completed);
    REQUIRE(search.groups.size() == 1);
    CHECK(search.groups[0].members == std::vector<FileRef>{{Side::source, 0}, {Side::destination, 0}});
    REQUIRE(search.errors.size() == 2);
    CHECK(search.errors[0].side == Side::source);
    CHECK(search.errors[0].relative_path == "b.jpg");
    CHECK(search.errors[0].error == std::errc::io_error);
    CHECK(search.errors[1].side == Side::destination);
    CHECK(search.errors[1].changed);
}

TEST_CASE("une source ou une destination qui disparait arrete la recherche")
{
    for (const auto side : {Side::source, Side::destination}) {
        Fixture fixture;
        fixture.add(Side::source, "a.jpg", content(5000, 3));
        fixture.add(Side::destination, "b.jpg", content(5000, 3));
        auto& memory = side == Side::source ? fixture.source : fixture.destination;
        memory.failing.insert(side == Side::source ? "a.jpg" : "b.jpg");
        memory.vanishes_on_failure = true;
        const auto search = fixture.find();
        CHECK(search.outcome == ScanOutcome::source_unavailable);
        CHECK(search.groups.empty());
    }
}

TEST_CASE("la recherche s'interrompt et remonte sa progression en octets")
{
    Fixture fixture;
    fixture.add(Side::source, "a.mp4", content(big, 1));
    fixture.add(Side::destination, "b.mp4", content(big, 1));

    std::vector<DuplicateProgress> seen;
    const auto search = fixture.find({}, [&](const DuplicateProgress& p) { seen.push_back(p); });
    REQUIRE(search.groups.size() == 1);
    REQUIRE(seen.size() == 4);
    CHECK(seen[1].phase == DuplicateProgress::Phase::heads);
    CHECK(seen[1].bytes_done == 2 * duplicate_head_bytes);
    CHECK(seen[1].bytes_total == 2 * duplicate_head_bytes);
    CHECK(seen[3].phase == DuplicateProgress::Phase::wholes);
    CHECK(seen[3].bytes_done == 2 * big);
    CHECK(seen[3].bytes_total == 2 * big);

    std::stop_source stop;
    const auto interrupted = fixture.find(stop.get_token(), [&](const DuplicateProgress&) { stop.request_stop(); });
    CHECK(interrupted.outcome == ScanOutcome::interrupted);
    CHECK(interrupted.groups.empty());
}

TEST_CASE("la recherche s'arrete au milieu d'un gros fichier")
{
    Fixture fixture;
    constexpr std::size_t huge = 3 * 1024 * 1024;
    fixture.add(Side::source, "a.mp4", content(huge, 1));
    fixture.add(Side::destination, "b.mp4", content(huge, 1));

    std::stop_source stop;
    const auto search = fixture.find(stop.get_token(), [&](const DuplicateProgress& p) {
        if (p.phase == DuplicateProgress::Phase::wholes)
            stop.request_stop();
    });
    CHECK(search.outcome == ScanOutcome::interrupted);
    // Les deux débuts, puis un seul bloc de 1 Mio du premier fichier entier.
    CHECK(fixture.bytes_read() == 2 * duplicate_head_bytes + transfertphoto::hash_block_size);
}
