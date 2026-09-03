#ifndef LILYGO_UI_STORE_HTTP_CLIENT_HPP
#define LILYGO_UI_STORE_HTTP_CLIENT_HPP

#include "system/process.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

struct HttpTextResult {
  std::string body;
  std::string error;

  [[nodiscard]] explicit operator bool() const noexcept {
    return error.empty();
  }
};

struct HttpDownloadResult {
  std::string error;

  [[nodiscard]] explicit operator bool() const noexcept {
    return error.empty();
  }
};

class HttpClient {
public:
  virtual ~HttpClient() = default;
  [[nodiscard]] virtual HttpTextResult get(const std::string &url,
                                           std::size_t maximum_size,
                                           bool allow_file = false) = 0;
  [[nodiscard]] virtual HttpDownloadResult
  download(const std::string &url, const std::filesystem::path &destination,
           std::uint64_t maximum_size, bool allow_file = false) = 0;
};

class CurlHttpClient final : public HttpClient {
public:
  explicit CurlHttpClient(ProcessRunner &runner) noexcept : runner_(runner) {}

  [[nodiscard]] HttpTextResult get(const std::string &url,
                                   std::size_t maximum_size,
                                   bool allow_file = false) override;
  [[nodiscard]] HttpTextResult get_binary(const std::string &url,
                                          std::size_t maximum_size,
                                          bool allow_file = false);
  [[nodiscard]] HttpDownloadResult
  download(const std::string &url, const std::filesystem::path &destination,
           std::uint64_t maximum_size, bool allow_file = false) override;

private:
  ProcessRunner &runner_;
};

[[nodiscard]] bool valid_transport_url(const std::string &url,
                                       bool allow_file) noexcept;

#endif
