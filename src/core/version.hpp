#pragma once

#include <string_view>

namespace transfertphoto {

// Version of the application, as declared by the build ("major.minor.patch").
[[nodiscard]] std::string_view version() noexcept;

} // namespace transfertphoto
