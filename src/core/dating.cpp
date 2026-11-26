#include "core/dating.hpp"

#include "core/media_format.hpp"
#include "core/name_date.hpp"
#include "core/photo_date.hpp"
#include "core/video_date.hpp"

#include <chrono>
#include <optional>
#include <system_error>
#include <utility>

namespace transfertphoto {
namespace {

Dating abandoned(ScanOutcome outcome, std::vector<SourceError> errors)
{
    Dating dating;
    dating.outcome = outcome;
    dating.errors = std::move(errors);
    return dating;
}

// Le format selon lequel lire les métadonnées : celui que révèle le contenu
// quand il contredit l'extension (un JPEG nommé .PNG reste un JPEG).
MediaFormat format_to_read(const MediaItem& item)
{
    if (item.signature.verdict == SignatureVerdict::mismatch && item.signature.detected)
        return *item.signature.detected;
    return item.format;
}

struct MetadataDate {
    std::optional<LocalDateTime> date;
    std::error_code source_error;
};

MetadataDate read_metadata_date(Source& source, const MediaItem& item)
{
    const auto format = format_to_read(item);
    const auto& entry = item.entry;
    if (kind_of(format) == MediaKind::photo) {
        auto read = read_photo_date(source, entry.relative_path, entry.size);
        return {read.date, read.source_error};
    }
    auto read = read_video_date(source, entry.relative_path, entry.size, format);
    return {read.date, read.source_error};
}

LocalDateTime modification_date(const SourceEntry& entry)
{
    const auto seconds = std::chrono::floor<std::chrono::seconds>(entry.modified.time_since_epoch());
    return local_from_unix_seconds(seconds.count());
}

} // namespace

Dating date_media(Source& source, const Inventory& inventory, std::stop_token stop,
                  const std::function<void(const DatingProgress&)>& on_progress)
{
    Dating dating;
    const auto total = inventory.media.size();
    dating.media.reserve(total);

    for (std::size_t i = 0; i < total; ++i) {
        if (stop.stop_requested())
            return abandoned(ScanOutcome::interrupted, std::move(dating.errors));

        const auto& item = inventory.media[i];
        MediaDates dates;

        const auto metadata = read_metadata_date(source, item);
        if (metadata.source_error) {
            // Un fichier illisible peut être le premier signe d'une source qui
            // s'en va : téléphone débranché, carte retirée.
            if (!source.is_available())
                return abandoned(ScanOutcome::source_unavailable, std::move(dating.errors));
            dating.errors.push_back({item.entry.relative_path, metadata.source_error});
        }
        if (metadata.date)
            dates.candidates.push_back({*metadata.date, DateOrigin::metadata, true});
        if (const auto name = date_from_file_name(item.entry.relative_path))
            dates.candidates.push_back({name->value, DateOrigin::file_name, name->has_time});
        dates.candidates.push_back({modification_date(item.entry), DateOrigin::modification, true});

        dating.media.push_back(std::move(dates));
        if (on_progress)
            on_progress(DatingProgress{i + 1, total});
    }
    return dating;
}

} // namespace transfertphoto
