#include "domain/debian_version.hpp"

#include <cctype>
#include <string_view>

namespace {

struct DebianVersion {
  unsigned long long epoch = 0;
  std::string_view upstream;
  std::string_view revision;
};

DebianVersion split_version(std::string_view value) noexcept {
  DebianVersion result;
  const auto colon = value.find(':');
  auto remainder = value;
  if (colon != std::string_view::npos) {
    for (std::size_t index = 0; index < colon; ++index) {
      if (!std::isdigit(static_cast<unsigned char>(value[index])))
        break;
      const auto digit = static_cast<unsigned>(value[index] - '0');
      if (result.epoch <= (~0ULL - digit) / 10ULL)
        result.epoch = result.epoch * 10ULL + digit;
    }
    remainder = value.substr(colon + 1);
  }
  const auto dash = remainder.rfind('-');
  if (dash == std::string_view::npos) {
    result.upstream = remainder;
    result.revision = "0";
  } else {
    result.upstream = remainder.substr(0, dash);
    result.revision = remainder.substr(dash + 1);
  }
  return result;
}

int character_order(char value) noexcept {
  if (value == '~')
    return -1;
  if (value == '\0')
    return 0;
  if (std::isalpha(static_cast<unsigned char>(value)))
    return static_cast<unsigned char>(value);
  return static_cast<unsigned char>(value) + 256;
}

int compare_part(std::string_view left, std::string_view right) noexcept {
  std::size_t left_index = 0;
  std::size_t right_index = 0;
  while (left_index < left.size() || right_index < right.size()) {
    while ((left_index < left.size() &&
            !std::isdigit(static_cast<unsigned char>(left[left_index]))) ||
           (right_index < right.size() &&
            !std::isdigit(static_cast<unsigned char>(right[right_index])))) {
      const auto left_character =
          left_index < left.size() &&
                  !std::isdigit(static_cast<unsigned char>(left[left_index]))
              ? left[left_index]
              : '\0';
      const auto right_character =
          right_index < right.size() &&
                  !std::isdigit(static_cast<unsigned char>(right[right_index]))
              ? right[right_index]
              : '\0';
      const auto left_order = character_order(left_character);
      const auto right_order = character_order(right_character);
      if (left_order != right_order)
        return left_order < right_order ? -1 : 1;
      if (left_character != '\0')
        ++left_index;
      if (right_character != '\0')
        ++right_index;
    }
    auto left_start = left_index;
    auto right_start = right_index;
    while (left_index < left.size() &&
           std::isdigit(static_cast<unsigned char>(left[left_index])))
      ++left_index;
    while (right_index < right.size() &&
           std::isdigit(static_cast<unsigned char>(right[right_index])))
      ++right_index;
    while (left_start < left_index && left[left_start] == '0')
      ++left_start;
    while (right_start < right_index && right[right_start] == '0')
      ++right_start;
    const auto left_digits = left.substr(left_start, left_index - left_start);
    const auto right_digits =
        right.substr(right_start, right_index - right_start);
    if (left_digits.size() != right_digits.size())
      return left_digits.size() < right_digits.size() ? -1 : 1;
    if (left_digits != right_digits)
      return left_digits < right_digits ? -1 : 1;
  }
  return 0;
}

} // namespace

int debian_version_compare(std::string_view left,
                           std::string_view right) noexcept {
  const auto left_version = split_version(left);
  const auto right_version = split_version(right);
  if (left_version.epoch != right_version.epoch)
    return left_version.epoch < right_version.epoch ? -1 : 1;
  const auto upstream =
      compare_part(left_version.upstream, right_version.upstream);
  return upstream != 0
             ? upstream
             : compare_part(left_version.revision, right_version.revision);
}
