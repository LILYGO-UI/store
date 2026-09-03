#include "registry/root_cache.hpp"

#include "app_identity.h"
#include "registry/sha256.hpp"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <unistd.h>
#include <utility>

namespace {

constexpr std::uintmax_t kMaximumRootSize = 64U * 1024U;

std::filesystem::path cache_directory(const std::string &root_url,
                                      std::error_code &error) {
  std::filesystem::path root;
  if (const auto *configured = std::getenv("XDG_CACHE_HOME");
      configured && *configured &&
      std::filesystem::path(configured).is_absolute()) {
    root = configured;
  } else if (const auto *home = std::getenv("HOME"); home && *home) {
    root = std::filesystem::path(home) / ".cache";
  } else {
    root = std::filesystem::temp_directory_path(error) /
           (std::string(LILYGO_UI_STORE_PACKAGE_NAME) + "-" +
            std::to_string(getuid()));
  }
  if (error)
    return {};
  return root / LILYGO_UI_STORE_PACKAGE_NAME / "registry" / sha256(root_url);
}

} // namespace

RegistryRootCacheResult
RegistryRootCache::load(const std::string &root_url) const {
  std::error_code error;
  const auto directory = cache_directory(root_url, error);
  if (error || directory.empty())
    return {{}, "Unable to locate the Registry cache directory"};

  const auto destination = directory / "root.json";
  const auto status = std::filesystem::symlink_status(destination, error);
  if (error || !std::filesystem::is_regular_file(status) ||
      std::filesystem::is_symlink(status))
    return {{}, "No cached Registry root is available"};
  const auto size = std::filesystem::file_size(destination, error);
  if (error || size == 0 || size > kMaximumRootSize)
    return {{}, "The cached Registry root is invalid"};

  std::ifstream input(destination, std::ios::binary);
  std::string document(static_cast<std::size_t>(size), '\0');
  input.read(document.data(), static_cast<std::streamsize>(document.size()));
  if (!input || input.gcount() != static_cast<std::streamsize>(document.size()))
    return {{}, "Unable to read the cached Registry root"};
  return {std::move(document), {}};
}

std::string RegistryRootCache::store(const std::string &root_url,
                                     const std::string &document) const {
  if (document.empty() || document.size() > kMaximumRootSize)
    return "Registry root is too large to cache";

  std::error_code error;
  const auto directory = cache_directory(root_url, error);
  if (error || directory.empty())
    return "Unable to locate the Registry cache directory";
  const auto directory_status =
      std::filesystem::symlink_status(directory, error);
  if (!error && std::filesystem::is_symlink(directory_status))
    return "The Registry cache directory is not safe";
  error.clear();
  std::filesystem::create_directories(directory, error);
  if (error)
    return "Unable to create the Registry cache directory";
  std::filesystem::permissions(directory, std::filesystem::perms::owner_all,
                               std::filesystem::perm_options::replace, error);
  if (error)
    return "Unable to secure the Registry cache directory";

  const auto destination = directory / "root.json";
  const auto destination_status =
      std::filesystem::symlink_status(destination, error);
  if (!error && std::filesystem::is_symlink(destination_status))
    return "The Registry cache file is not safe";
  error.clear();

  static std::atomic<std::uint64_t> temporary_sequence{0};
  const auto temporary =
      directory / ("root.json.part-" + std::to_string(getpid()) + "-" +
                   std::to_string(temporary_sequence.fetch_add(1)));
  std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
  output.write(document.data(), static_cast<std::streamsize>(document.size()));
  output.close();
  if (!output) {
    std::filesystem::remove(temporary, error);
    return "Unable to write the Registry cache";
  }

  std::filesystem::rename(temporary, destination, error);
  if (error) {
    std::filesystem::remove(temporary, error);
    return "Unable to publish the cached Registry root";
  }
  return {};
}
