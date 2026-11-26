#pragma once

#include "core/inventory.hpp"
#include "core/source.hpp"

#include <functional>
#include <stop_token>

namespace transfertphoto {

// L'inventaire du dossier de destination, pour y chercher des doublons : même
// parcours et même classement que la source, sur toute l'arborescence,
// __A_TRIER__ compris. Seules ses photos et vidéos (Inventory::media) entrent
// dans la recherche ; leurs accompagnants les suivent.
//
// Une destination qui n'existe pas encore est un dossier vide : le premier
// transfert la créera. Celle qui disparaît pendant le parcours est perdue,
// comme une source. Dire qu'elle manque, et qu'un disque n'est peut-être pas
// branché, revient à la fenêtre.
[[nodiscard]] Inventory take_destination_inventory(
    Source& destination, std::stop_token stop,
    const std::function<void(const InventoryProgress&)>& on_progress = {});

} // namespace transfertphoto
