#include "registry/http_client.hpp"

#include <system_error>

namespace {

std::string protocol_option(bool allow_file) {
  return allow_file ? "=https,file" : "=https";
}

std::string command_error(const ProcessResult &result) {
  if (result.output_limit_exceeded)
    return "Response exceeded the size limit";
  if (!result.error.empty()) {
    auto value = result.error;
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r'))
      value.pop_back();
    if (!value.empty())
      return value;
  }
  return "Network request failed (curl exit code " +
         std::to_string(result.exit_code) + ")";
}

} // namespace

bool valid_transport_url(const std::string &url, bool allow_file) noexcept {
  return url.rfind("https://", 0) == 0 ||
         (allow_file && url.rfind("file://", 0) == 0);
}

HttpTextResult CurlHttpClient::get(const std::string &url,
                                   std::size_t maximum_size, bool allow_file) {
  if (!valid_transport_url(url, allow_file))
    return {{}, "Registry URL must use HTTPS"};
  const auto result = runner_.run(
      {"curl", "--fail", "--silent", "--show-error", "--location", "--proto",
       protocol_option(allow_file), "--proto-redir",
       protocol_option(allow_file), "--connect-timeout", "10", "--max-time",
       "30", "--retry", "2", "--header", "Accept: application/json", url},
      maximum_size);
  if (result.exit_code != 0 || result.output_limit_exceeded)
    return {{}, command_error(result)};
  return {result.output, {}};
}

HttpTextResult CurlHttpClient::get_binary(const std::string &url,
                                          std::size_t maximum_size,
                                          bool allow_file) {
  if (!valid_transport_url(url, allow_file))
    return {{}, "Image URL must use HTTPS"};
  const auto result = runner_.run(
      {"curl", "--fail", "--silent", "--show-error", "--location", "--proto",
       protocol_option(allow_file), "--proto-redir",
       protocol_option(allow_file), "--connect-timeout", "10", "--max-time",
       "30", "--retry", "2", "--header", "Accept: image/png", url},
      maximum_size);
  if (result.exit_code != 0 || result.output_limit_exceeded)
    return {{}, command_error(result)};
  return {result.output, {}};
}

HttpDownloadResult
CurlHttpClient::download(const std::string &url,
                         const std::filesystem::path &destination,
                         std::uint64_t maximum_size, bool allow_file) {
  if (!valid_transport_url(url, allow_file))
    return {"Package URL must use HTTPS"};
  const auto result = runner_.run({"curl",
                                   "--fail",
                                   "--silent",
                                   "--show-error",
                                   "--location",
                                   "--proto",
                                   protocol_option(allow_file),
                                   "--proto-redir",
                                   protocol_option(allow_file),
                                   "--connect-timeout",
                                   "10",
                                   "--max-time",
                                   "900",
                                   "--retry",
                                   "2",
                                   "--header",
                                   "Accept: application/octet-stream",
                                   "--max-filesize",
                                   std::to_string(maximum_size),
                                   "--output",
                                   destination.string(),
                                   url});
  if (result.exit_code != 0)
    return {command_error(result)};
  std::error_code error;
  const auto size = std::filesystem::file_size(destination, error);
  if (error)
    return {"Unable to read the downloaded package"};
  if (size > maximum_size)
    return {"Downloaded package exceeded the size limit"};
  return {};
}
