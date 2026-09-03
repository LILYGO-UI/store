#ifndef LILYGO_UI_STORE_DOMAIN_DEBIAN_VERSION_HPP
#define LILYGO_UI_STORE_DOMAIN_DEBIAN_VERSION_HPP

#include <string_view>

[[nodiscard]] int debian_version_compare(std::string_view left,
                                         std::string_view right) noexcept;

#endif
