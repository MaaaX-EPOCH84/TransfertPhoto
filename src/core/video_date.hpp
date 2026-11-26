#pragma once

#include "core/local_date_time.hpp"
#include "core/media_format.hpp"
#include "core/source.hpp"

#include <cstdint>
#include <optional>
#include <string_view>
#include <system_error>

namespace transfertphoto {

struct VideoDateRead {
    std::optional<LocalDateTime> date;
    // Une lecture de la source a échoué ; un conteneur abîmé n'en est pas une.
    std::error_code source_error;
};

// Lit la date de prise de vue d'une vidéo dans son conteneur, sans rien
// décoder. Selon le format :
//
//   MP4, MOV, M4V, 3GP : la clé com.apple.quicktime.creationdate de moov/meta
//     ou de moov/udta/meta, heure du lieu de prise de vue, sinon creation_time
//     de moov/mvhd, en UTC depuis 1904, convertie à l'heure locale de cette
//     machine ;
//   AVI : le bloc IDIT, heure de l'appareil ;
//   MKV : DateUTC de Segment/Info, en nanosecondes UTC depuis 2001, convertie ;
//   MTS, M2TS : la date MDPM que les caméscopes AVCHD glissent dans les
//     données utilisateur des SEI H.264, heure de l'appareil.
//
// Une date nulle vaut absence. Ne lève jamais, et ne lit que les boîtes ou les
// blocs utiles, où qu'ils soient dans le fichier.
[[nodiscard]] VideoDateRead read_video_date(Source& source, std::string_view relative_path,
                                            std::uint64_t size, MediaFormat format);

} // namespace transfertphoto
