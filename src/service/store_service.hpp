#ifndef LILYGO_UI_STORE_SERVICE_HPP
#define LILYGO_UI_STORE_SERVICE_HPP

#include "domain/store_model.hpp"
#include "registry/registry_client.hpp"
#include "system/package_manager.hpp"

#include <string>
#include <vector>

struct StoreRefreshResult {
  std::vector<StoreApp> apps;
  std::string snapshot_id;
  std::string error;
  std::string root_document;
  std::string cache_error;

  [[nodiscard]] explicit operator bool() const noexcept {
    return error.empty();
  }
};

struct StoreImageDownloadResult {
  std::string path;
  std::string error;

  [[nodiscard]] explicit operator bool() const noexcept {
    return error.empty();
  }
};

class StoreService {
public:
  StoreService(RegistryClient &registry, PackageManager &packages) noexcept
      : registry_(registry), packages_(packages) {}

  [[nodiscard]] StoreRefreshResult refresh(const std::string &root_url);
  [[nodiscard]] StoreRefreshResult refresh(const std::string &root_url,
                                           const std::string &root_document);

private:
  [[nodiscard]] StoreRefreshResult make_result(RegistryLoadResult loaded);

  RegistryClient &registry_;
  PackageManager &packages_;
};

[[nodiscard]] StoreRefreshResult refresh_store(const std::string &root_url);
[[nodiscard]] StoreRefreshResult
refresh_cached_store(const std::string &root_url);
[[nodiscard]] PackageOperationResult
install_store_package(const std::string &package,
                      const RegistryRelease &release);
[[nodiscard]] PackageOperationResult
remove_store_package(const std::string &package);
[[nodiscard]] StoreImageDownloadResult
download_store_image(const std::string &url, const std::string &cache_key);

#endif
