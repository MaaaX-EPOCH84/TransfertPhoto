#include "core/video_date.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace transfertphoto {
namespace {

using Bytes = std::vector<std::uint8_t>;

// Secondes du 1er janvier 1904 (QuickTime) et du 1er janvier 2001 (Matroska)
// au 1er janvier 1970.
constexpr std::int64_t seconds_from_1904_to_1970 = 2'082'844'800;
constexpr std::int64_t seconds_from_1970_to_2001 = 978'307'200;

// Tout ce qui se lit d'un coup dans une boîte ou un bloc. Une boîte meta ou
// un bloc Info plus gros que cela est abîmé, ou n'est pas ce qu'il dit.
constexpr std::size_t max_loaded = 1024 * 1024;

// Garde-fou contre un conteneur qui bouclerait ou s'étalerait sans fin.
constexpr int max_elements = 100'000;

std::uint32_t be32(const std::uint8_t* p)
{
    return static_cast<std::uint32_t>(p[0]) << 24 | static_cast<std::uint32_t>(p[1]) << 16
           | static_cast<std::uint32_t>(p[2]) << 8 | p[3];
}

std::uint64_t be64(const std::uint8_t* p)
{
    return static_cast<std::uint64_t>(be32(p)) << 32 | be32(p + 4);
}

std::uint32_t le32(const std::uint8_t* p)
{
    return static_cast<std::uint32_t>(p[3]) << 24 | static_cast<std::uint32_t>(p[2]) << 16
           | static_cast<std::uint32_t>(p[1]) << 8 | p[0];
}

bool is_type(const std::uint8_t* p, const char (&type)[5])
{
    return std::memcmp(p, type, 4) == 0;
}

// Lit le fichier par plages, et garde la première erreur de la source.
class Reader {
public:
    Reader(Source& source, std::string_view path, std::uint64_t size)
        : source_(source), path_(path), size_(size)
    {
    }

    [[nodiscard]] std::uint64_t size() const { return size_; }
    [[nodiscard]] const std::error_code& error() const { return error_; }

    // Jusqu'à count octets à partir de offset ; moins en fin de fichier.
    Bytes read_some(std::uint64_t offset, std::size_t count)
    {
        if (error_ || offset >= size_)
            return {};
        count = static_cast<std::size_t>(std::min<std::uint64_t>(count, size_ - offset));
        Bytes bytes(count);
        const auto read = source_.read_range(path_, offset, std::as_writable_bytes(std::span(bytes)), error_);
        bytes.resize(error_ ? 0 : read);
        return bytes;
    }

    // Exactement count octets, ou faux.
    bool read_exact(std::uint64_t offset, std::size_t count, Bytes& bytes)
    {
        bytes = read_some(offset, count);
        return bytes.size() == count;
    }

private:
    Source& source_;
    std::string_view path_;
    std::uint64_t size_;
    std::error_code error_;
};

// --- ISO BMFF : MP4, MOV, M4V, 3GP -----------------------------------------

struct Box {
    std::uint64_t start = 0;   // début de l'en-tête
    std::uint64_t payload = 0; // début du contenu
    std::uint64_t end = 0;     // fin de la boîte
    std::array<char, 4> type{};
};

// La boîte qui commence à position, sans dépasser limit.
bool read_box(Reader& reader, std::uint64_t position, std::uint64_t limit, Box& box)
{
    Bytes header;
    if (limit - position < 8 || !reader.read_exact(position, 8, header))
        return false;
    std::uint64_t size = be32(header.data());
    std::uint64_t header_size = 8;
    if (size == 1) {
        Bytes large;
        if (limit - position < 16 || !reader.read_exact(position + 8, 8, large))
            return false;
        size = be64(large.data());
        header_size = 16;
    } else if (size == 0) {
        size = limit - position; // jusqu'à la fin du fichier
    }
    if (size < header_size || size > limit - position)
        return false;
    box.start = position;
    box.payload = position + header_size;
    box.end = position + size;
    std::memcpy(box.type.data(), header.data() + 4, 4);
    return true;
}

// Même chose dans un contenu déjà en mémoire.
struct MemoryBox {
    std::size_t payload = 0;
    std::size_t end = 0;
    std::array<char, 4> type{};
};

bool memory_box(const Bytes& data, std::size_t position, std::size_t limit, MemoryBox& box)
{
    if (limit > data.size() || position > limit || limit - position < 8)
        return false;
    std::uint64_t size = be32(data.data() + position);
    std::size_t header_size = 8;
    if (size == 1) {
        if (limit - position < 16)
            return false;
        size = be64(data.data() + position + 8);
        header_size = 16;
    } else if (size == 0) {
        size = limit - position;
    }
    if (size < header_size || size > limit - position)
        return false;
    box.payload = position + header_size;
    box.end = position + static_cast<std::size_t>(size);
    std::memcpy(box.type.data(), data.data() + position + 4, 4);
    return true;
}

bool box_is(const std::array<char, 4>& type, const char (&expected)[5])
{
    return std::memcmp(type.data(), expected, 4) == 0;
}

// La clé com.apple.quicktime.creationdate d'une boîte meta de type mdta.
std::optional<LocalDateTime> apple_creation_date(const Bytes& meta)
{
    // La meta de QuickTime n'a pas d'en-tête version et drapeaux, celle de la
    // norme ISO en a un : on regarde où commence la boîte hdlr.
    std::size_t start = 0;
    if (meta.size() >= 12 && is_type(meta.data() + 8, "hdlr"))
        start = 4;
    else if (meta.size() < 8 || !is_type(meta.data() + 4, "hdlr"))
        return std::nullopt;

    std::uint32_t wanted_index = 0;
    std::size_t ilst_payload = 0;
    std::size_t ilst_end = 0;
    MemoryBox box;
    for (std::size_t position = start; memory_box(meta, position, meta.size(), box); position = box.end) {
        if (box_is(box.type, "keys")) {
            // Version et drapeaux, nombre d'entrées, puis taille, espace de
            // noms et nom de chaque clé ; les index commencent à 1.
            if (box.end - box.payload < 8)
                continue;
            const std::uint32_t count = be32(meta.data() + box.payload + 4);
            std::size_t entry = box.payload + 8;
            for (std::uint32_t index = 1; index <= count && box.end - entry >= 8; ++index) {
                const std::uint32_t entry_size = be32(meta.data() + entry);
                if (entry_size < 8 || entry_size > box.end - entry)
                    break;
                const std::string_view name(reinterpret_cast<const char*>(meta.data() + entry + 8),
                                            entry_size - 8);
                if (name == "com.apple.quicktime.creationdate")
                    wanted_index = index;
                entry += entry_size;
            }
        } else if (box_is(box.type, "ilst")) {
            ilst_payload = box.payload;
            ilst_end = box.end;
        }
    }
    if (wanted_index == 0 || ilst_end == 0)
        return std::nullopt;

    // Chaque élément de ilst porte l'index de sa clé à la place d'un type, et
    // sa valeur dans une boîte data : type, langue, puis le texte.
    MemoryBox item;
    for (std::size_t position = ilst_payload; memory_box(meta, position, ilst_end, item); position = item.end) {
        if (be32(reinterpret_cast<const std::uint8_t*>(item.type.data())) != wanted_index)
            continue;
        MemoryBox data;
        for (std::size_t inner = item.payload; memory_box(meta, inner, item.end, data); inner = data.end) {
            if (!box_is(data.type, "data") || data.end - data.payload < 8)
                continue;
            const std::string_view text(reinterpret_cast<const char*>(meta.data() + data.payload + 8),
                                        data.end - data.payload - 8);
            // « 2023-12-25T18:04:00+0100 » : l'heure du lieu, le décalage ensuite.
            return parse_exif_date_time(text);
        }
    }
    return std::nullopt;
}

std::optional<LocalDateTime> meta_creation_date(Reader& reader, const Box& meta_box)
{
    if (meta_box.end - meta_box.payload > max_loaded)
        return std::nullopt;
    Bytes meta;
    if (!reader.read_exact(meta_box.payload, static_cast<std::size_t>(meta_box.end - meta_box.payload), meta))
        return std::nullopt;
    return apple_creation_date(meta);
}

std::optional<LocalDateTime> bmff_date(Reader& reader)
{
    Box moov;
    bool found = false;
    Box box;
    int visited = 0;
    for (std::uint64_t position = 0; ++visited < max_elements && read_box(reader, position, reader.size(), box);
         position = box.end) {
        if (box_is(box.type, "moov")) {
            moov = box;
            found = true;
            break;
        }
    }
    if (!found)
        return std::nullopt;

    std::optional<LocalDateTime> from_mvhd;
    std::optional<LocalDateTime> from_apple;
    visited = 0;
    for (std::uint64_t position = moov.payload; ++visited < max_elements && read_box(reader, position, moov.end, box);
         position = box.end) {
        if (box_is(box.type, "mvhd")) {
            // Version et drapeaux, puis la date de création sur 32 ou 64 bits.
            Bytes head;
            if (!reader.read_exact(box.payload, 12, head))
                continue;
            const std::uint64_t created = head[0] == 1 ? be64(head.data() + 4) : be32(head.data() + 4);
            if (created != 0 && created <= static_cast<std::uint64_t>(INT64_MAX - seconds_from_1904_to_1970))
                from_mvhd = local_from_unix_seconds(static_cast<std::int64_t>(created) - seconds_from_1904_to_1970);
        } else if (box_is(box.type, "meta")) {
            if (!from_apple)
                from_apple = meta_creation_date(reader, box);
        } else if (box_is(box.type, "udta")) {
            // ffmpeg range les clés dans moov/udta/meta plutôt que dans moov/meta.
            Box inner;
            int inner_visited = 0;
            for (std::uint64_t at = box.payload;
                 ++inner_visited < max_elements && read_box(reader, at, box.end, inner); at = inner.end) {
                if (box_is(inner.type, "meta") && !from_apple)
                    from_apple = meta_creation_date(reader, inner);
            }
        }
    }
    return from_apple ? from_apple : from_mvhd;
}

// --- AVI --------------------------------------------------------------------

constexpr std::array<const char*, 12> month_names{"jan", "feb", "mar", "apr", "may", "jun",
                                                   "jul", "aug", "sep", "oct", "nov", "dec"};

// Les nombres d'un texte, dans l'ordre, et s'il se termine par PM.
std::vector<unsigned> numbers_in(std::string_view text)
{
    std::vector<unsigned> numbers;
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] >= '0' && text[i] <= '9') {
            unsigned value = 0;
            std::size_t digits = 0;
            while (i < text.size() && text[i] >= '0' && text[i] <= '9' && digits < 9) {
                value = value * 10 + static_cast<unsigned>(text[i] - '0');
                ++i;
                ++digits;
            }
            numbers.push_back(value);
        } else {
            ++i;
        }
    }
    return numbers;
}

// Le texte d'IDIT. Les appareils l'écrivent à leur façon :
// « Mon Mar 10 15:04:43 2003 » (format AVI d'origine), « 2002-12-16  15:35:01 »,
// « 2005/11/28/ 09:19 », « 2001/ 1/27  1:42PM ».
std::optional<LocalDateTime> parse_idit(std::string_view text)
{
    const auto end = text.find('\0');
    if (end != std::string_view::npos)
        text = text.substr(0, end);

    LocalDateTime value;
    const auto numbers = numbers_in(text);

    std::string lower;
    for (const char c : text)
        lower += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    unsigned month = 0;
    for (std::size_t i = 0; i < month_names.size(); ++i) {
        if (lower.find(month_names[i]) != std::string::npos) {
            month = static_cast<unsigned>(i + 1);
            break;
        }
    }

    if (month != 0) {
        // jour, heure, minute, seconde, année
        if (numbers.size() < 5)
            return std::nullopt;
        value.year = static_cast<int>(numbers[4]);
        value.month = month;
        value.day = numbers[0];
        value.hour = numbers[1];
        value.minute = numbers[2];
        value.second = numbers[3];
    } else {
        // année, mois, jour, heure, minute, et peut-être seconde
        if (numbers.size() < 5)
            return std::nullopt;
        value.year = static_cast<int>(numbers[0]);
        value.month = numbers[1];
        value.day = numbers[2];
        value.hour = numbers[3];
        value.minute = numbers[4];
        value.second = numbers.size() >= 6 ? numbers[5] : 0;
        const auto last = lower.find_last_not_of(" \t\r\n");
        if (last != std::string::npos && last >= 1 && lower.compare(last - 1, 2, "pm") == 0
            && value.hour < 12)
            value.hour += 12;
    }
    if (value.year < 1000 || !is_valid(value))
        return std::nullopt;
    return value;
}

std::optional<LocalDateTime> riff_date(Reader& reader, std::uint64_t begin, std::uint64_t end, int depth,
                                       int& visited)
{
    Bytes header;
    for (std::uint64_t position = begin; position < end && end - position >= 8 && ++visited < max_elements;) {
        if (!reader.read_exact(position, 8, header))
            return std::nullopt;
        const std::uint64_t size = le32(header.data() + 4);
        const std::uint64_t chunk_end = position + 8 + size;
        if (chunk_end > end)
            return std::nullopt;
        if (is_type(header.data(), "IDIT") && size <= 256) {
            Bytes text;
            if (reader.read_exact(position + 8, static_cast<std::size_t>(size), text))
                return parse_idit(std::string_view(reinterpret_cast<const char*>(text.data()), text.size()));
        } else if (is_type(header.data(), "LIST") && size >= 4 && depth < 4) {
            Bytes list_type;
            // Les images elles-mêmes, inutile d'y descendre.
            if (reader.read_exact(position + 8, 4, list_type) && !is_type(list_type.data(), "movi")) {
                if (auto date = riff_date(reader, position + 12, chunk_end, depth + 1, visited))
                    return date;
            }
        }
        position = chunk_end + (size % 2); // les blocs sont alignés sur deux octets
    }
    return std::nullopt;
}

std::optional<LocalDateTime> avi_date(Reader& reader)
{
    Bytes header;
    if (!reader.read_exact(0, 12, header) || !is_type(header.data(), "RIFF") || !is_type(header.data() + 8, "AVI "))
        return std::nullopt;
    const std::uint64_t end = std::min<std::uint64_t>(reader.size(), 8 + static_cast<std::uint64_t>(le32(header.data() + 4)));
    int visited = 0;
    return riff_date(reader, 12, end, 0, visited);
}

// --- Matroska ---------------------------------------------------------------

constexpr std::uint32_t ebml_header_id = 0x1A45DFA3;
constexpr std::uint32_t segment_id = 0x18538067;
constexpr std::uint32_t info_id = 0x1549A966;
constexpr std::uint32_t date_utc_id = 0x4461;
constexpr std::uint64_t unknown_size = UINT64_MAX;

struct Element {
    std::uint32_t id = 0;
    std::uint64_t payload = 0;
    std::uint64_t size = 0; // unknown_size si la taille n'est pas dite
};

// Un entier de longueur variable : la longueur se lit au premier bit à 1 du
// premier octet. L'identifiant garde ce bit, la taille le perd.
bool read_vint(const Bytes& bytes, std::size_t& position, bool keep_marker, std::uint64_t& value, bool& all_ones)
{
    if (position >= bytes.size() || bytes[position] == 0)
        return false;
    const std::uint8_t first = bytes[position];
    std::size_t length = 1;
    while ((first & (0x80 >> (length - 1))) == 0)
        ++length;
    if (position + length > bytes.size())
        return false;
    const std::uint8_t marker = static_cast<std::uint8_t>(0x80 >> (length - 1));
    value = keep_marker ? first : (first & (marker - 1));
    all_ones = (first & (marker - 1)) == marker - 1;
    for (std::size_t i = 1; i < length; ++i) {
        value = value << 8 | bytes[position + i];
        all_ones = all_ones && bytes[position + i] == 0xFF;
    }
    position += length;
    return true;
}

bool read_element(Reader& reader, std::uint64_t position, Element& element)
{
    const Bytes head = reader.read_some(position, 12);
    std::size_t at = 0;
    std::uint64_t id = 0;
    std::uint64_t size = 0;
    bool all_ones = false;
    if (!read_vint(head, at, true, id, all_ones) || at > 4)
        return false;
    if (!read_vint(head, at, false, size, all_ones))
        return false;
    element.id = static_cast<std::uint32_t>(id);
    element.payload = position + at;
    element.size = all_ones ? unknown_size : size;
    return true;
}

std::optional<LocalDateTime> mkv_date(Reader& reader)
{
    Element element;
    if (!read_element(reader, 0, element) || element.id != ebml_header_id || element.size == unknown_size)
        return std::nullopt;
    std::uint64_t position = element.payload + element.size;

    if (!read_element(reader, position, element) || element.id != segment_id)
        return std::nullopt;
    const std::uint64_t segment_end = element.size == unknown_size
                                          ? reader.size()
                                          : std::min(reader.size(), element.payload + element.size);

    int visited = 0;
    for (position = element.payload; position < segment_end && ++visited < max_elements;) {
        if (!read_element(reader, position, element) || element.size == unknown_size)
            return std::nullopt; // un Cluster de taille inconnue : on ne saura pas l'enjamber
        if (element.size > segment_end - element.payload)
            return std::nullopt;
        if (element.id == info_id && element.size <= max_loaded) {
            Bytes info;
            if (!reader.read_exact(element.payload, static_cast<std::size_t>(element.size), info))
                return std::nullopt;
            std::size_t at = 0;
            while (at < info.size()) {
                std::uint64_t id = 0;
                std::uint64_t size = 0;
                bool all_ones = false;
                if (!read_vint(info, at, true, id, all_ones) || !read_vint(info, at, false, size, all_ones)
                    || all_ones || size > info.size() - at)
                    return std::nullopt;
                if (id == date_utc_id && size == 8) {
                    // Entier signé, en nanosecondes depuis le 1er janvier 2001 UTC.
                    const auto nanoseconds = static_cast<std::int64_t>(be64(info.data() + at));
                    if (nanoseconds == 0)
                        return std::nullopt;
                    std::int64_t seconds = nanoseconds / 1'000'000'000;
                    if (nanoseconds % 1'000'000'000 < 0)
                        --seconds;
                    return local_from_unix_seconds(seconds + seconds_from_1970_to_2001);
                }
                at += static_cast<std::size_t>(size);
            }
            return std::nullopt;
        }
        position = element.payload + element.size;
    }
    return std::nullopt;
}

// --- MPEG-TS des AVCHD ------------------------------------------------------

// Ce qu'on parcourt du début du fichier : la première image porte la date.
constexpr std::size_t mts_window = 4 * 1024 * 1024;

// Identifiant des données utilisateur des caméscopes, suivi de « MDPM ».
constexpr std::array<std::uint8_t, 20> mdpm_marker{0x17, 0xEE, 0x8C, 0x60, 0xF8, 0x4D, 0x11, 0xD9, 0x8C, 0xD6,
                                                   0x08, 0x00, 0x20, 0x0C, 0x9A, 0x66, 'M',  'D',  'P',  'M'};

bool from_bcd(std::uint8_t byte, unsigned& value)
{
    const unsigned high = byte >> 4;
    const unsigned low = byte & 0x0F;
    if (high > 9 || low > 9)
        return false;
    value = high * 10 + low;
    return true;
}

// Les entrées MDPM : un nombre, puis des groupes de cinq octets, une
// étiquette et quatre de données. 0x18 : fuseau, année en deux octets BCD,
// mois ; 0x19 : jour, heure, minute, seconde, en BCD.
std::optional<LocalDateTime> parse_mdpm(const Bytes& data, std::size_t position)
{
    if (position >= data.size())
        return std::nullopt;
    const unsigned count = data[position++];
    const std::uint8_t* first = nullptr;
    const std::uint8_t* second = nullptr;
    for (unsigned i = 0; i < count && position + 5 <= data.size(); ++i, position += 5) {
        if (data[position] == 0x18)
            first = data.data() + position + 1;
        else if (data[position] == 0x19)
            second = data.data() + position + 1;
    }
    if (first == nullptr || second == nullptr)
        return std::nullopt;

    unsigned century = 0;
    unsigned year = 0;
    LocalDateTime value;
    if (!from_bcd(first[1], century) || !from_bcd(first[2], year) || !from_bcd(first[3], value.month)
        || !from_bcd(second[0], value.day) || !from_bcd(second[1], value.hour) || !from_bcd(second[2], value.minute)
        || !from_bcd(second[3], value.second))
        return std::nullopt;
    value.year = static_cast<int>(century * 100 + year);
    if (value.year == 0 || !is_valid(value))
        return std::nullopt;
    return value;
}

// Retire les octets d'émulation que H.264 glisse après deux zéros, sans quoi
// une date à « 00 00 » ne se retrouverait pas.
Bytes without_emulation_prevention(const Bytes& data)
{
    Bytes out;
    out.reserve(data.size());
    int zeros = 0;
    for (const std::uint8_t byte : data) {
        if (zeros >= 2 && byte == 0x03) {
            zeros = 0;
            continue;
        }
        out.push_back(byte);
        zeros = byte == 0 ? zeros + 1 : 0;
    }
    return out;
}

std::optional<LocalDateTime> mts_date(Reader& reader)
{
    const Bytes window = reader.read_some(0, mts_window);
    // Paquets de 188 octets, ou de 192 avec quatre octets d'horodatage devant
    // (M2TS des AVCHD).
    std::size_t packet = 0;
    std::size_t offset = 0;
    if (window.size() >= 2 * 188 && window[0] == 0x47 && window[188] == 0x47) {
        packet = 188;
    } else if (window.size() >= 2 * 192 && window[4] == 0x47 && window[196] == 0x47) {
        packet = 192;
        offset = 4;
    } else {
        return std::nullopt;
    }

    // Le contenu de chaque flux, mis bout à bout : la SEI peut tenir sur
    // plusieurs paquets, entrecoupés de ceux du son.
    std::map<unsigned, Bytes> streams;
    for (std::size_t start = 0; start + packet <= window.size(); start += packet) {
        const std::uint8_t* p = window.data() + start + offset;
        if (p[0] != 0x47)
            continue;
        const unsigned pid = (p[1] & 0x1Fu) << 8 | p[2];
        const unsigned control = (p[3] >> 4) & 0x03u;
        std::size_t payload = 4;
        if (control & 0x02u)
            payload += 1u + p[4];
        if ((control & 0x01u) == 0 || payload >= 188)
            continue;
        auto& stream = streams[pid];
        stream.insert(stream.end(), p + payload, p + 188);
    }

    for (const auto& [pid, raw] : streams) {
        const Bytes stream = without_emulation_prevention(raw);
        auto found = std::search(stream.begin(), stream.end(), mdpm_marker.begin(), mdpm_marker.end());
        while (found != stream.end()) {
            const auto position = static_cast<std::size_t>(found - stream.begin()) + mdpm_marker.size();
            if (auto date = parse_mdpm(stream, position))
                return date;
            found = std::search(found + 1, stream.end(), mdpm_marker.begin(), mdpm_marker.end());
        }
    }
    return std::nullopt;
}

} // namespace

VideoDateRead read_video_date(Source& source, std::string_view relative_path, std::uint64_t size,
                              MediaFormat format)
{
    Reader reader(source, relative_path, size);
    VideoDateRead result;
    switch (format) {
    case MediaFormat::mp4:
    case MediaFormat::mov:
    case MediaFormat::m4v:
    case MediaFormat::three_gp:
        result.date = bmff_date(reader);
        break;
    case MediaFormat::avi:
        result.date = avi_date(reader);
        break;
    case MediaFormat::mkv:
        result.date = mkv_date(reader);
        break;
    case MediaFormat::mpeg_ts:
        result.date = mts_date(reader);
        break;
    default:
        break;
    }
    if (result.date && !is_valid(*result.date))
        result.date.reset();
    result.source_error = reader.error();
    return result;
}

} // namespace transfertphoto
