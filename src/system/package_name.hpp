#ifndef LILYGO_UI_STORE_PACKAGE_NAME_HPP
#define LILYGO_UI_STORE_PACKAGE_NAME_HPP

#include <algorithm>
#include <string_view>

// Registry runtimes can use Debian names such as meshtasticd or libfoo1.2.
// Never accept options, architecture selectors, patterns or shell syntax.
inline bool valid_debian_package_name(std::string_view value) noexcept
{
    const auto alphanumeric = [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); };
    return value.size() >= 2 && value.size() <= 128 && alphanumeric(value.front()) &&
           std::all_of(value.begin(), value.end(),
                       [&](char c) { return alphanumeric(c) || c == '+' || c == '-' || c == '.'; });
}

#endif
