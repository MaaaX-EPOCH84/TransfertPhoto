#include "core/destination.hpp"

namespace transfertphoto {

Inventory take_destination_inventory(Source& destination, std::stop_token stop,
                                     const std::function<void(const InventoryProgress&)>& on_progress)
{
    if (!destination.is_available())
        return {};
    return take_inventory(destination, stop, on_progress);
}

} // namespace transfertphoto
