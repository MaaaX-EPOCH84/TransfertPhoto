#pragma once

#include <string_view>

namespace transfertphoto {

// Version de l'application, telle que la déclare la construction ("majeur.mineur.correctif").
[[nodiscard]] std::string_view version() noexcept;

} // namespace transfertphoto
