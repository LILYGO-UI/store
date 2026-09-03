#ifndef LILYGO_UI_STORE_SHA256_HPP
#define LILYGO_UI_STORE_SHA256_HPP

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

class Sha256 {
public:
  Sha256() noexcept;

  void update(const void *data, std::size_t size) noexcept;
  [[nodiscard]] std::string finish() noexcept;

private:
  void transform(const std::uint8_t block[64]) noexcept;

  std::uint32_t state_[8]{};
  std::uint8_t buffer_[64]{};
  std::uint64_t bit_count_ = 0;
  std::size_t buffer_size_ = 0;
  bool finished_ = false;
};

[[nodiscard]] std::string sha256(std::string_view value) noexcept;
[[nodiscard]] bool sha256_file(const std::filesystem::path &path,
                               std::string &digest,
                               std::string &error) noexcept;

#endif
