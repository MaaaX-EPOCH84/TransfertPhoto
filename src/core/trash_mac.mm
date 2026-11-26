// La corbeille de macOS, par Foundation.

#include "core/trash.hpp"

#import <Foundation/Foundation.h>

namespace fs = std::filesystem;

namespace transfertphoto {
namespace {

NSURL* url_of(const fs::path& file)
{
    const auto text = fs::absolute(file).u8string();
    NSString* path = [[NSString alloc] initWithBytes:text.data()
                                              length:text.size()
                                            encoding:NSUTF8StringEncoding];
    NSURL* url = [NSURL fileURLWithPath:path];
    [path release];
    return url;
}

bool flag(NSURL* url, NSURLResourceKey key)
{
    NSNumber* value = nil;
    if (![url getResourceValue:&value forKey:key error:nil] || value == nil)
        return false;
    return [value boolValue];
}

} // namespace

bool volume_has_trash(const fs::path& file)
{
    @autoreleasepool {
        NSURL* url = url_of(file);
        // Carte, clé, disque éjectable, ou volume réseau (non local).
        if (flag(url, NSURLVolumeIsRemovableKey) || flag(url, NSURLVolumeIsEjectableKey))
            return false;
        NSNumber* local = nil;
        if ([url getResourceValue:&local forKey:NSURLVolumeIsLocalKey error:nil] && local != nil && ![local boolValue])
            return false;
        return true;
    }
}

TrashResult move_to_trash(const fs::path& file)
{
    TrashResult result;
    if (!volume_has_trash(file)) {
        result.outcome = TrashOutcome::unavailable;
        return result;
    }
    @autoreleasepool {
        NSError* error = nil;
        if ([[NSFileManager defaultManager] trashItemAtURL:url_of(file) resultingItemURL:nil error:&error]) {
            result.outcome = TrashOutcome::trashed;
        } else {
            result.error = std::make_error_code(std::errc::io_error);
            if (error != nil && [[error domain] isEqualToString:NSPOSIXErrorDomain])
                result.error = std::error_code(static_cast<int>([error code]), std::generic_category());
        }
    }
    return result;
}

} // namespace transfertphoto
