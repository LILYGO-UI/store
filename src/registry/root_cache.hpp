#ifndef LILYGO_UI_STORE_REGISTRY_ROOT_CACHE_HPP
#define LILYGO_UI_STORE_REGISTRY_ROOT_CACHE_HPP

#include <string>

struct RegistryRootCacheResult {
  std::string document;
  std::string error;

  [[nodiscard]] explicit operator bool() const noexcept {
    return error.empty();
  }
};

class RegistryRootCache {
public:
  [[nodiscard]] RegistryRootCacheResult load(const std::string &root_url) const;
  [[nodiscard]] std::string store(const std::string &root_url,
                                  const std::string &document) const;
};

#endif
