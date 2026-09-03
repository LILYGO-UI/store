#ifndef LILYGO_UI_STORE_REGISTRY_CLIENT_HPP
#define LILYGO_UI_STORE_REGISTRY_CLIENT_HPP

#include "registry/http_client.hpp"

#include <cstdint>
#include <string>
#include <vector>

inline constexpr const char *kDefaultRegistryRoot =
    "https://lilygo-ui.github.io/packages/v1/root.json";

struct RegistryAuthor {
  std::string name;
  std::string email;
  std::string github;
};

struct RegistryPermission {
  std::string id;
  std::string reason;
  std::string access;
  std::string scope;
};

struct RegistryScreenshot {
  std::string url;
  std::string caption;
};

struct RegistryRelease {
  std::string version;
  std::string architecture;
  std::string status;
  std::uint64_t size = 0;
  std::string sha256;
  std::string url;
  std::string min_appkit_version;
  std::string published_at;
  std::string reason;
};

struct RegistryApplication {
  std::string app_id;
  std::string package;
  std::string title;
  std::string summary;
  std::string description;
  std::vector<RegistryAuthor> authors;
  std::vector<std::string> categories;
  std::string license;
  std::string source_repo;
  std::string homepage;
  std::string icon_url;
  std::vector<RegistryScreenshot> screenshots;
  std::vector<RegistryPermission> permissions;
  std::vector<RegistryRelease> releases;
  std::string latest_version;
};

struct RegistryCatalog {
  std::string snapshot_id;
  std::string generated_at;
  std::vector<RegistryApplication> apps;
};

struct RegistryLoadResult {
  RegistryCatalog catalog;
  std::string error;
  std::string root_document;

  [[nodiscard]] explicit operator bool() const noexcept {
    return error.empty();
  }
};

class RegistryClient {
public:
  explicit RegistryClient(HttpClient &http) noexcept : http_(http) {}

  [[nodiscard]] RegistryLoadResult
  load(const std::string &root_url = kDefaultRegistryRoot);
  [[nodiscard]] RegistryLoadResult
  load_document(const std::string &root_url, const std::string &root_document);

private:
  HttpClient &http_;
};

#endif
