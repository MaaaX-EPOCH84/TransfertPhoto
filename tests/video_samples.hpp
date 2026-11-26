#pragma once

// Conteneurs vidéo fabriqués octet par octet, sans image ni son : ce qui sert
// à les dater, et assez du reste pour qu'il faille l'enjamber.

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace test_support {

using VideoBytes = std::vector<std::uint8_t>;

inline void put_be(VideoBytes& out, std::uint64_t value, int bytes)
{
    for (int i = bytes - 1; i >= 0; --i)
        out.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF));
}

inline void put_le(VideoBytes& out, std::uint64_t value, int bytes)
{
    for (int i = 0; i < bytes; ++i)
        out.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF));
}

inline void append(VideoBytes& out, const VideoBytes& more)
{
    out.insert(out.end(), more.begin(), more.end());
}

inline void append(VideoBytes& out, const std::string& text)
{
    out.insert(out.end(), text.begin(), text.end());
}

// --- ISO BMFF -------------------------------------------------------------

inline VideoBytes box(const std::string& type, const VideoBytes& payload)
{
    VideoBytes out;
    put_be(out, 8 + payload.size(), 4);
    append(out, type);
    append(out, payload);
    return out;
}

// mvhd, version 0 (32 bits) ou 1 (64 bits), le reste à zéro.
inline VideoBytes mvhd(std::uint64_t created_since_1904, int version = 0)
{
    VideoBytes payload{static_cast<std::uint8_t>(version), 0, 0, 0};
    const int width = version == 1 ? 8 : 4;
    put_be(payload, created_since_1904, width); // création
    put_be(payload, created_since_1904, width); // modification
    put_be(payload, 600, 4);                    // échelle de temps
    put_be(payload, 0, width);                  // durée
    payload.resize(payload.size() + 80, 0);
    return box("mvhd", payload);
}

// meta de QuickTime, à clés mdta : chaque clé et sa valeur texte.
// iso_full_box : la variante de la norme ISO, avec version et drapeaux.
inline VideoBytes mdta_meta(const std::vector<std::pair<std::string, std::string>>& entries,
                            bool iso_full_box = false)
{
    VideoBytes hdlr_payload(4, 0); // version, drapeaux
    put_be(hdlr_payload, 0, 4);    // prédéfini
    append(hdlr_payload, std::string("mdta"));
    hdlr_payload.resize(hdlr_payload.size() + 12, 0);
    hdlr_payload.push_back(0); // nom vide

    VideoBytes keys_payload(4, 0);
    put_be(keys_payload, entries.size(), 4);
    VideoBytes ilst_payload;
    std::uint32_t index = 1;
    for (const auto& [key, value] : entries) {
        put_be(keys_payload, 8 + key.size(), 4);
        append(keys_payload, std::string("mdta"));
        append(keys_payload, key);

        VideoBytes data_payload;
        put_be(data_payload, 1, 4); // UTF-8
        put_be(data_payload, 0, 4); // langue
        append(data_payload, value);
        VideoBytes item;
        put_be(item, 8 + 8 + data_payload.size(), 4);
        put_be(item, index++, 4);
        append(item, box("data", data_payload));
        append(ilst_payload, item);
    }

    VideoBytes payload;
    if (iso_full_box)
        payload.resize(4, 0);
    append(payload, box("hdlr", hdlr_payload));
    append(payload, box("keys", keys_payload));
    append(payload, box("ilst", ilst_payload));
    return box("meta", payload);
}

struct Mp4Options {
    std::optional<std::uint64_t> mvhd_created = std::nullopt; // secondes depuis 1904
    int mvhd_version = 0;
    std::optional<VideoBytes> meta = std::nullopt;
    bool meta_in_udta = false; // comme l'écrit ffmpeg
    bool moov_at_end = false;
    std::size_t mdat_size = 1000;
    std::string brand = "isom";
};

inline VideoBytes mp4(const Mp4Options& options)
{
    VideoBytes ftyp_payload;
    append(ftyp_payload, options.brand);
    put_be(ftyp_payload, 0, 4);
    append(ftyp_payload, options.brand);

    VideoBytes moov_payload;
    if (options.mvhd_created)
        append(moov_payload, mvhd(*options.mvhd_created, options.mvhd_version));
    append(moov_payload, box("trak", VideoBytes(40, 0)));
    if (options.meta && options.meta_in_udta)
        append(moov_payload, box("udta", *options.meta));
    else if (options.meta)
        append(moov_payload, *options.meta);

    VideoBytes out = box("ftyp", ftyp_payload);
    const auto moov = box("moov", moov_payload);
    const auto mdat = box("mdat", VideoBytes(options.mdat_size, 0xAB));
    if (options.moov_at_end) {
        append(out, mdat);
        append(out, moov);
    } else {
        append(out, moov);
        append(out, mdat);
    }
    return out;
}

// --- AVI --------------------------------------------------------------------

inline VideoBytes riff_chunk(const std::string& id, const VideoBytes& data)
{
    VideoBytes out;
    append(out, id);
    put_le(out, data.size(), 4);
    append(out, data);
    if (data.size() % 2 != 0)
        out.push_back(0);
    return out;
}

inline VideoBytes riff_list(const std::string& type, const VideoBytes& content)
{
    VideoBytes data;
    append(data, type);
    append(data, content);
    return riff_chunk("LIST", data);
}

// Un AVI dont la liste hdrl porte un IDIT avec ce texte, tel quel.
inline VideoBytes avi(const std::optional<std::string>& idit)
{
    VideoBytes hdrl = riff_chunk("avih", VideoBytes(56, 0));
    append(hdrl, riff_list("strl", riff_chunk("strh", VideoBytes(56, 0))));
    if (idit)
        append(hdrl, riff_chunk("IDIT", VideoBytes(idit->begin(), idit->end())));

    VideoBytes content;
    append(content, std::string("AVI "));
    append(content, riff_list("hdrl", hdrl));
    append(content, riff_list("movi", riff_chunk("00dc", VideoBytes(301, 0x11))));
    append(content, riff_chunk("idx1", VideoBytes(16, 0)));

    VideoBytes out;
    append(out, std::string("RIFF"));
    put_le(out, content.size(), 4);
    append(out, content);
    return out;
}

// --- Matroska ---------------------------------------------------------------

inline void put_ebml_id(VideoBytes& out, std::uint32_t id)
{
    int bytes = id > 0xFFFFFF ? 4 : id > 0xFFFF ? 3 : id > 0xFF ? 2 : 1;
    put_be(out, id, bytes);
}

// Taille sur huit octets, toujours : la forme la plus simple à écrire.
inline void put_ebml_size(VideoBytes& out, std::uint64_t size)
{
    out.push_back(0x01);
    put_be(out, size, 7);
}

inline VideoBytes ebml(std::uint32_t id, const VideoBytes& payload)
{
    VideoBytes out;
    put_ebml_id(out, id);
    put_ebml_size(out, payload.size());
    append(out, payload);
    return out;
}

struct MkvOptions {
    std::optional<std::int64_t> date_utc_ns = std::nullopt; // depuis 2001-01-01 UTC
    bool unknown_segment_size = false;
};

inline VideoBytes mkv(const MkvOptions& options)
{
    VideoBytes header_payload;
    VideoBytes doc_type(std::begin("matroska"), std::end("matroska") - 1);
    append(header_payload, ebml(0x4282, doc_type));

    VideoBytes info_payload;
    VideoBytes scale;
    put_be(scale, 1'000'000, 3);
    append(info_payload, ebml(0x2AD7B1, scale));
    if (options.date_utc_ns) {
        VideoBytes date;
        put_be(date, static_cast<std::uint64_t>(*options.date_utc_ns), 8);
        append(info_payload, ebml(0x4461, date));
    }

    VideoBytes segment_payload;
    append(segment_payload, ebml(0x114D9B74, VideoBytes(20, 0xEC))); // SeekHead, contenu sans objet
    append(segment_payload, ebml(0x1549A966, info_payload));
    append(segment_payload, ebml(0x1F43B675, VideoBytes(500, 0x00))); // Cluster

    VideoBytes out = ebml(0x1A45DFA3, header_payload);
    put_ebml_id(out, 0x18538067);
    if (options.unknown_segment_size) {
        out.push_back(0x01);
        put_be(out, 0x00FFFFFFFFFFFFFFull, 7);
    } else {
        put_ebml_size(out, segment_payload.size());
    }
    append(out, segment_payload);
    return out;
}

// --- MPEG-TS des AVCHD ------------------------------------------------------

struct MdpmDate {
    unsigned year, month, day, hour, minute, second;
};

inline std::uint8_t bcd(unsigned value)
{
    return static_cast<std::uint8_t>((value / 10) << 4 | (value % 10));
}

// Insère les octets d'émulation comme le fait un codeur H.264.
inline VideoBytes with_emulation_prevention(const VideoBytes& raw)
{
    VideoBytes out;
    int zeros = 0;
    for (const auto byte : raw) {
        if (zeros >= 2 && byte <= 3) {
            out.push_back(0x03);
            zeros = 0;
        }
        out.push_back(byte);
        zeros = byte == 0 ? zeros + 1 : 0;
    }
    return out;
}

// Une NAL SEI H.264 dont les données utilisateur portent la date MDPM.
inline VideoBytes mdpm_sei(const MdpmDate& date)
{
    VideoBytes user_data{0x17, 0xEE, 0x8C, 0x60, 0xF8, 0x4D, 0x11, 0xD9, 0x8C, 0xD6,
                         0x08, 0x00, 0x20, 0x0C, 0x9A, 0x66, 'M',  'D',  'P',  'M'};
    user_data.push_back(3); // trois entrées
    user_data.insert(user_data.end(), {0x13, 0, 0, 0, 0}); // une autre étiquette d'abord
    user_data.insert(user_data.end(),
                     {0x18, 0x02, bcd(date.year / 100), bcd(date.year % 100), bcd(date.month)});
    user_data.insert(user_data.end(),
                     {0x19, bcd(date.day), bcd(date.hour), bcd(date.minute), bcd(date.second)});

    VideoBytes sei{0x05}; // données utilisateur non enregistrées
    sei.push_back(static_cast<std::uint8_t>(user_data.size()));
    append(sei, user_data);
    sei.push_back(0x80); // fin de la NAL

    VideoBytes nal{0x00, 0x00, 0x00, 0x01, 0x06};
    append(nal, with_emulation_prevention(sei));
    return nal;
}

struct MtsOptions {
    std::optional<MdpmDate> date = std::nullopt;
    std::size_t packet_size = 188;     // 188, ou 192 pour un M2TS
    std::size_t filler_before_sei = 0; // pour faire chevaucher deux paquets
};

// CRC des tables MPEG-TS : polynôme 0x04C11DB7, sans réflexion.
inline std::uint32_t mpeg_crc32(const VideoBytes& data)
{
    std::uint32_t crc = 0xFFFFFFFFu;
    for (const auto byte : data) {
        crc ^= static_cast<std::uint32_t>(byte) << 24;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 0x80000000u) ? (crc << 1) ^ 0x04C11DB7u : crc << 1;
    }
    return crc;
}

// Une section de table (PAT ou PMT), CRC compris.
inline VideoBytes psi_section(std::uint8_t table_id, std::uint16_t id, const VideoBytes& body)
{
    VideoBytes section{table_id};
    put_be(section, 0xB000u | (5 + body.size() + 4), 2); // longueur : en-tête, corps, CRC
    put_be(section, id, 2);
    section.insert(section.end(), {0xC1, 0x00, 0x00}); // version, section 0 sur 0
    append(section, body);
    put_be(section, mpeg_crc32(section), 4);
    return section;
}

inline VideoBytes mts(const MtsOptions& options)
{
    // Un PES vidéo : une NAL de remplissage, puis la SEI.
    VideoBytes pes{0x00, 0x00, 0x01, 0xE0, 0x00, 0x00, 0x80, 0x00, 0x00};
    VideoBytes filler{0x00, 0x00, 0x00, 0x01, 0x09, 0xF0};
    filler.resize(filler.size() + options.filler_before_sei, 0x55);
    append(pes, filler);
    if (options.date)
        append(pes, mdpm_sei(*options.date));
    pes.resize(pes.size() + 400, 0x66);

    VideoBytes out;
    unsigned continuity_video = 0;
    unsigned continuity_audio = 0;
    auto put_packet = [&](unsigned pid, unsigned& continuity, const std::uint8_t* data, std::size_t size,
                          bool start) {
        if (options.packet_size == 192)
            put_be(out, 0, 4); // horodatage
        out.push_back(0x47);
        out.push_back(static_cast<std::uint8_t>((start ? 0x40 : 0x00) | ((pid >> 8) & 0x1F)));
        out.push_back(static_cast<std::uint8_t>(pid & 0xFF));
        if (size < 184) {
            // Champ d'adaptation pour bourrer le paquet.
            out.push_back(static_cast<std::uint8_t>(0x30 | (continuity++ & 0x0F)));
            const std::size_t adaptation = 184 - size - 1;
            out.push_back(static_cast<std::uint8_t>(adaptation));
            if (adaptation > 0) {
                out.push_back(0x00);
                out.insert(out.end(), adaptation - 1, 0xFF);
            }
        } else {
            out.push_back(static_cast<std::uint8_t>(0x10 | (continuity++ & 0x0F)));
        }
        out.insert(out.end(), data, data + size);
    };

    // Les tables qui disent où est l'image : un lecteur comme exiftool en a
    // besoin, celui du projet s'en passe.
    VideoBytes pat_body;
    put_be(pat_body, 1, 2);       // programme 1
    put_be(pat_body, 0xE100, 2);  // sa PMT sur le PID 0x100
    VideoBytes pmt_body;
    put_be(pmt_body, 0xF011, 2);  // PCR sur le PID de l'image
    put_be(pmt_body, 0xF000, 2);  // pas de descripteur de programme
    pmt_body.insert(pmt_body.end(), {0x1B, 0xF0, 0x11, 0xF0, 0x00}); // H.264 sur 0x1011
    pmt_body.insert(pmt_body.end(), {0x81, 0xF1, 0x00, 0xF0, 0x00}); // AC-3 sur 0x1100
    for (const auto& [pid, table] : {std::pair{0u, psi_section(0x00, 1, pat_body)},
                                     std::pair{0x100u, psi_section(0x02, 1, pmt_body)}}) {
        VideoBytes payload{0x00}; // pointeur de section
        append(payload, table);
        unsigned continuity = 0;
        put_packet(pid, continuity, payload.data(), payload.size(), true);
    }

    const VideoBytes audio(184, 0x77);
    for (std::size_t at = 0; at < pes.size(); at += 184) {
        const std::size_t size = std::min<std::size_t>(184, pes.size() - at);
        put_packet(0x1011, continuity_video, pes.data() + at, size, at == 0);
        // Un paquet de son entre deux paquets d'image.
        put_packet(0x1100, continuity_audio, audio.data(), audio.size(), at == 0);
    }
    return out;
}

} // namespace test_support
