#include "system/package_manager.hpp"

#include "app_identity.h"
#include "registry/json.hpp"
#include "registry/sha256.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string_view>
#include <system_error>
#include <unistd.h>
#include <vector>

namespace {

class TemporaryPackage {
public:
  TemporaryPackage() {
    std::string pattern = "/tmp/";
    pattern += LILYGO_UI_STORE_PACKAGE_NAME;
    pattern += "-XXXXXX";
    std::vector<char> mutable_pattern(pattern.cbegin(), pattern.cend());
    mutable_pattern.push_back('\0');
    const auto descriptor = mkstemp(mutable_pattern.data());
    if (descriptor >= 0) {
      close(descriptor);
      path_ = mutable_pattern.data();
    }
  }

  ~TemporaryPackage() {
    if (!path_.empty()) {
      std::error_code ignored;
      std::filesystem::remove(path_, ignored);
    }
  }

  [[nodiscard]] const std::filesystem::path &path() const noexcept {
    return path_;
  }

private:
  std::filesystem::path path_;
};

std::string trimmed(std::string value) {
  while (!value.empty() &&
         (value.back() == '\n' || value.back() == '\r' || value.back() == ' '))
    value.pop_back();
  return value;
}

std::string process_error(const ProcessResult &result,
                          const std::string &fallback) {
  const auto detail = trimmed(result.error);
  return detail.empty() ? fallback : detail;
}

bool valid_package_name(const std::string &package) {
  constexpr std::string_view prefix = "lilygo-ui-";
  if (package.rfind(prefix, 0) != 0 || package.size() == prefix.size())
    return false;
  for (std::size_t index = prefix.size(); index < package.size(); ++index) {
    const auto value = package[index];
    if (!((value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') ||
          value == '-'))
      return false;
  }
  return package.back() != '-';
}

constexpr std::size_t kMaximumGitHubReleaseSize = 512U * 1024U;
constexpr std::string_view kGitHubReleasePrefix =
    "https://github.com/LILYGO-UI/packages/releases/download/";
constexpr std::string_view kGitHubApiAssetPrefix =
    "https://api.github.com/repos/LILYGO-UI/packages/releases/assets/";

std::optional<std::string>
github_api_asset_url(HttpClient &http, const std::string &package,
                     const RegistryRelease &release) {
  const auto tag = "apt-pool-" + package;
  const auto asset_name = "sha256-" + release.sha256 + ".deb";
  const auto expected_url =
      std::string(kGitHubReleasePrefix) + tag + "/" + asset_name;
  if (release.url != expected_url)
    return std::nullopt;

  const auto metadata_url =
      "https://api.github.com/repos/LILYGO-UI/packages/releases/tags/" + tag;
  const auto response = http.get(metadata_url, kMaximumGitHubReleaseSize);
  if (!response)
    return std::nullopt;
  const auto parsed = parse_json(response.body);
  const auto *root = parsed ? parsed.value.object() : nullptr;
  if (!root)
    return std::nullopt;
  const auto tag_member = root->find("tag_name");
  const auto assets_member = root->find("assets");
  if (tag_member == root->end() || !tag_member->second.string() ||
      *tag_member->second.string() != tag || assets_member == root->end() ||
      !assets_member->second.array())
    return std::nullopt;

  const auto expected_digest = "sha256:" + release.sha256;
  for (const auto &value : *assets_member->second.array()) {
    const auto *asset = value.object();
    if (!asset)
      continue;
    const auto name = asset->find("name");
    const auto size = asset->find("size");
    const auto digest = asset->find("digest");
    const auto state = asset->find("state");
    const auto url = asset->find("url");
    if (name == asset->end() || !name->second.string() ||
        *name->second.string() != asset_name || size == asset->end() ||
        !size->second.integer() || *size->second.integer() <= 0 ||
        static_cast<std::uint64_t>(*size->second.integer()) != release.size ||
        digest == asset->end() || !digest->second.string() ||
        *digest->second.string() != expected_digest || state == asset->end() ||
        !state->second.string() || *state->second.string() != "uploaded" ||
        url == asset->end() || !url->second.string())
      continue;

    const auto &asset_url = *url->second.string();
    if (asset_url.rfind(kGitHubApiAssetPrefix, 0) != 0 ||
        asset_url.size() == kGitHubApiAssetPrefix.size() ||
        !std::all_of(asset_url.cbegin() + static_cast<std::ptrdiff_t>(
                                              kGitHubApiAssetPrefix.size()),
                     asset_url.cend(), [](unsigned char value) {
                       return value >= '0' && value <= '9';
                     }))
      continue;
    return asset_url;
  }
  return std::nullopt;
}

HttpDownloadResult download_release(HttpClient &http,
                                    const std::string &package,
                                    const RegistryRelease &release,
                                    const std::filesystem::path &destination,
                                    bool allow_file) {
  if (const auto asset_url = github_api_asset_url(http, package, release)) {
    const auto downloaded =
        http.download(*asset_url, destination, release.size, false);
    if (downloaded)
      return {};
  }
  return http.download(release.url, destination, release.size, allow_file);
}

std::vector<std::string> privileged_dpkg(std::vector<std::string> arguments) {
  if (const auto *direct = std::getenv("LILYGO_UI_STORE_DPKG_DIRECT");
      direct && std::string_view(direct) == "1") {
    arguments.insert(arguments.begin(), "dpkg");
    return arguments;
  }
  arguments.insert(arguments.begin(), "dpkg");
  arguments.insert(arguments.begin(), "pkexec");
  return arguments;
}

} // namespace

InstalledPackageResult PackageManager::query(const std::string &package) {
  if (!valid_package_name(package))
    return {false, {}, "Invalid package name"};
  const auto result =
      runner_.run({"dpkg-query", "--show",
                   "--showformat=${db:Status-Abbrev}\t${Version}", package});
  if (result.exit_code != 0)
    return {};
  const auto separator = result.output.find('\t');
  if (separator == std::string::npos)
    return {false, {}, "Unable to parse installed package state"};
  const auto status = result.output.substr(0, separator);
  const auto version = trimmed(result.output.substr(separator + 1));
  if (status.rfind("ii", 0) != 0 || version.empty())
    return {};
  return {true, version, {}};
}

std::string PackageManager::architecture() {
  if (const auto *configured = std::getenv("LILYGO_UI_STORE_ARCHITECTURE");
      configured && *configured)
    return configured;
  const auto result = runner_.run({"dpkg", "--print-architecture"});
  if (result.exit_code == 0) {
    const auto value = trimmed(result.output);
    if (!value.empty())
      return value;
  }
#if defined(__aarch64__) || defined(_M_ARM64)
  return "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
  return "amd64";
#else
  return {};
#endif
}

PackageOperationResult PackageManager::install(const std::string &package,
                                               const RegistryRelease &release) {
  if (!valid_package_name(package) || release.status != "published" ||
      release.size == 0 || release.sha256.size() != 64)
    return {{}, "Invalid Registry package record"};
  TemporaryPackage temporary;
  if (temporary.path().empty())
    return {{}, "Unable to create temporary download file"};
  const bool allow_file = release.url.rfind("file://", 0) == 0;
  const auto downloaded =
      download_release(http_, package, release, temporary.path(), allow_file);
  if (!downloaded)
    return {{}, "Package download failed: " + downloaded.error};
  std::error_code filesystem_error;
  const auto size =
      std::filesystem::file_size(temporary.path(), filesystem_error);
  if (filesystem_error || size != release.size)
    return {{}, "Downloaded package size does not match Registry"};
  std::string digest;
  std::string hash_error;
  if (!sha256_file(temporary.path(), digest, hash_error))
    return {{}, hash_error};
  if (digest != release.sha256)
    return {{}, "Downloaded package SHA-256 mismatch"};

  const auto installed = runner_.run(
      privileged_dpkg({"--install", "--", temporary.path().string()}),
      4 * 1024 * 1024);
  if (installed.exit_code != 0)
    return {{}, process_error(installed, "dpkg installation failed")};
  const auto state = query(package);
  if (!state.installed || state.version != release.version)
    return {{}, "Installed version does not match the target release"};
  return {state.version, {}};
}

PackageOperationResult PackageManager::remove(const std::string &package) {
  if (!valid_package_name(package))
    return {{}, "Invalid package name"};
  if (package == LILYGO_UI_STORE_PACKAGE_NAME)
    return {{}, "The Store cannot remove itself"};
  const auto removed = runner_.run(privileged_dpkg({"--remove", "--", package}),
                                   4 * 1024 * 1024);
  if (removed.exit_code != 0)
    return {{}, process_error(removed, "dpkg removal failed")};
  const auto state = query(package);
  if (state.installed)
    return {{}, "Package remains installed after dpkg removal"};
  return {{}, {}};
}
