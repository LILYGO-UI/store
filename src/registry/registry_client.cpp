#include "registry/registry_client.hpp"

#include "registry/json.hpp"
#include "registry/sha256.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <stdexcept>
#include <utility>

namespace {

constexpr std::size_t kMaximumRootSize = 64 * 1024;
constexpr std::size_t kMaximumIndexSize = 4 * 1024 * 1024;
constexpr std::size_t kMaximumDetailSize = 2 * 1024 * 1024;
constexpr std::size_t kMaximumApps = 5000;
constexpr std::size_t kMaximumReleases = 256;
constexpr std::int64_t kMaximumDebSize = 2LL * 1024 * 1024 * 1024 - 1;

class RegistryError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

const JsonValue &member(const JsonValue::Object &object,
                        std::string_view name) {
  const auto found = object.find(name);
  if (found == object.end())
    throw RegistryError("Registry is missing field " + std::string(name));
  return found->second;
}

const JsonValue::Object &object_value(const JsonValue &value,
                                      std::string_view context) {
  const auto *object = value.object();
  if (!object)
    throw RegistryError(std::string(context) + " must be an object");
  return *object;
}

const JsonValue::Array &array_value(const JsonValue &value,
                                    std::string_view context) {
  const auto *array = value.array();
  if (!array)
    throw RegistryError(std::string(context) + " must be an array");
  return *array;
}

std::string string_value(const JsonValue &value, std::string_view context) {
  const auto *text = value.string();
  if (!text)
    throw RegistryError(std::string(context) + " must be a string");
  return *text;
}

std::string required_string(const JsonValue::Object &object,
                            std::string_view name) {
  return string_value(member(object, name), name);
}

std::string optional_string(const JsonValue::Object &object,
                            std::string_view name) {
  const auto found = object.find(name);
  return found == object.end() ? std::string{}
                               : string_value(found->second, name);
}

std::int64_t required_integer(const JsonValue::Object &object,
                              std::string_view name) {
  const auto *value = member(object, name).integer();
  if (!value)
    throw RegistryError(std::string(name) + " must be an integer");
  return *value;
}

void require_protocol(const JsonValue::Object &object) {
  if (required_integer(object, "protocol_version") != 1)
    throw RegistryError("Unsupported Registry protocol version");
}

bool lowercase_hex(const std::string &value, std::size_t length) {
  return value.size() == length &&
         std::all_of(value.cbegin(), value.cend(), [](unsigned char character) {
           return std::isdigit(character) ||
                  (character >= static_cast<unsigned char>('a') &&
                   character <= static_cast<unsigned char>('f'));
         });
}

std::string registry_base(const std::string &root_url) {
  constexpr std::string_view suffix = "/v1/root.json";
  if (root_url.size() <= suffix.size() ||
      root_url.compare(root_url.size() - suffix.size(), suffix.size(),
                       suffix) != 0)
    throw RegistryError("Registry root URL must end with /v1/root.json");
  return root_url.substr(0, root_url.size() - suffix.size());
}

std::string normalized_registry_url(const std::string &url) {
  if (url.rfind("https://", 0) != 0)
    return url;

  constexpr std::size_t authority_begin = sizeof("https://") - 1;
  const auto authority_end = url.find_first_of("/?#", authority_begin);
  const auto end =
      authority_end == std::string::npos ? url.size() : authority_end;
  const auto userinfo_end = url.rfind('@', end);
  const auto host_begin =
      userinfo_end != std::string::npos && userinfo_end >= authority_begin
          ? userinfo_end + 1
          : authority_begin;

  auto normalized = url;
  std::transform(normalized.begin() + static_cast<std::ptrdiff_t>(host_begin),
                 normalized.begin() + static_cast<std::ptrdiff_t>(end),
                 normalized.begin() + static_cast<std::ptrdiff_t>(host_begin),
                 [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });
  return normalized;
}

bool same_registry_url(const std::string &left, const std::string &right) {
  return normalized_registry_url(left) == normalized_registry_url(right);
}

JsonValue parse_document(const std::string &body, std::string_view name) {
  auto parsed = parse_json(body);
  if (!parsed)
    throw RegistryError(std::string(name) +
                        " is not valid JSON: " + parsed.error);
  return std::move(parsed.value);
}

std::vector<std::string> parse_strings(const JsonValue &value,
                                       std::string_view context) {
  std::vector<std::string> result;
  for (const auto &item : array_value(value, context))
    result.push_back(string_value(item, context));
  return result;
}

RegistryAuthor parse_author(const JsonValue &value) {
  const auto &object = object_value(value, "author");
  RegistryAuthor result;
  result.name = required_string(object, "name");
  result.email = optional_string(object, "email");
  result.github = optional_string(object, "github");
  if (result.name.empty() || (result.email.empty() && result.github.empty()))
    throw RegistryError("Author information is incomplete");
  return result;
}

RegistryPermission parse_permission(const JsonValue &value) {
  const auto &object = object_value(value, "permission");
  RegistryPermission result;
  result.id = required_string(object, "id");
  result.reason = required_string(object, "reason");
  result.access = optional_string(object, "access");
  result.scope = optional_string(object, "scope");
  if (result.id.empty() || result.reason.empty())
    throw RegistryError("Permission information is incomplete");
  return result;
}

RegistryScreenshot parse_screenshot(const JsonValue &value, bool allow_file) {
  const auto &object = object_value(value, "screenshot");
  RegistryScreenshot result{required_string(object, "url"),
                            required_string(object, "caption")};
  if (!valid_transport_url(result.url, allow_file))
    throw RegistryError("Screenshot URL must use HTTPS");
  return result;
}

RegistryRelease parse_release(const JsonValue &value, bool allow_file) {
  const auto &object = object_value(value, "release");
  RegistryRelease result;
  result.version = required_string(object, "version");
  result.architecture = required_string(object, "architecture");
  result.status = required_string(object, "status");
  const auto size = required_integer(object, "size");
  if (size <= 0 || size > kMaximumDebSize)
    throw RegistryError("Package size exceeds the Registry limit");
  result.size = static_cast<std::uint64_t>(size);
  result.sha256 = required_string(object, "sha256");
  result.url = required_string(object, "url");
  result.min_appkit_version = required_string(object, "min_appkit_version");
  result.published_at = required_string(object, "published_at");
  result.reason = optional_string(object, "reason");
  if (result.status != "published" && result.status != "yanked" &&
      result.status != "revoked")
    throw RegistryError("Invalid package status");
  if (!lowercase_hex(result.sha256, 64))
    throw RegistryError("Invalid package SHA-256");
  if (!valid_transport_url(result.url, allow_file))
    throw RegistryError("Package URL must use HTTPS");
  if (result.status == "yanked" && result.reason.empty())
    throw RegistryError("Yanked release is missing a reason");
  return result;
}

RegistryApplication parse_detail(const std::string &body, bool allow_file) {
  const auto document = parse_document(body, "app detail");
  const auto &object = object_value(document, "app detail");
  require_protocol(object);
  RegistryApplication result;
  result.app_id = required_string(object, "app_id");
  result.package = required_string(object, "package");
  result.title = required_string(object, "title");
  result.summary = required_string(object, "summary");
  result.description = required_string(object, "description");
  result.categories = parse_strings(member(object, "categories"), "categories");
  result.license = required_string(object, "license");
  result.source_repo = required_string(object, "source_repo");
  result.homepage = required_string(object, "homepage");
  result.icon_url = required_string(object, "icon_url");
  if (!valid_transport_url(result.source_repo, allow_file) ||
      !valid_transport_url(result.homepage, allow_file) ||
      !valid_transport_url(result.icon_url, allow_file))
    throw RegistryError("App detail contains a non-HTTPS URL");
  for (const auto &author : array_value(member(object, "authors"), "authors"))
    result.authors.push_back(parse_author(author));
  for (const auto &screenshot :
       array_value(member(object, "screenshots"), "screenshots"))
    result.screenshots.push_back(parse_screenshot(screenshot, allow_file));
  for (const auto &permission :
       array_value(member(object, "permissions"), "permissions"))
    result.permissions.push_back(parse_permission(permission));
  const auto &releases = array_value(member(object, "releases"), "releases");
  if (releases.size() > kMaximumReleases)
    throw RegistryError("Release count exceeds the client limit");
  for (const auto &release : releases)
    result.releases.push_back(parse_release(release, allow_file));
  if (result.app_id.empty() || result.package.empty() || result.title.empty() ||
      result.summary.empty() || result.authors.empty() ||
      result.categories.empty())
    throw RegistryError("Required app detail fields are empty");
  return result;
}

struct IndexApplication {
  std::string app_id;
  std::string package;
  std::string title;
  std::string summary;
  std::vector<std::string> categories;
  std::string icon_url;
  std::string latest_version;
  std::string detail_url;
};

IndexApplication parse_index_app(const JsonValue &value, bool allow_file) {
  const auto &object = object_value(value, "index app");
  IndexApplication result;
  result.app_id = required_string(object, "app_id");
  result.package = required_string(object, "package");
  result.title = required_string(object, "title");
  result.summary = required_string(object, "summary");
  result.categories = parse_strings(member(object, "categories"), "categories");
  result.icon_url = required_string(object, "icon_url");
  result.latest_version = required_string(object, "latest_version");
  result.detail_url = required_string(object, "detail_url");
  if (!valid_transport_url(result.icon_url, allow_file) ||
      !valid_transport_url(result.detail_url, allow_file))
    throw RegistryError("Registry index contains a non-HTTPS URL");
  return result;
}

} // namespace

RegistryLoadResult RegistryClient::load(const std::string &root_url) {
  const bool allow_file = root_url.rfind("file://", 0) == 0;
  if (!valid_transport_url(root_url, allow_file))
    return {{}, "Registry root URL must use HTTPS", {}};
  const auto root_response = http_.get(root_url, kMaximumRootSize, allow_file);
  if (!root_response)
    return {{}, "Unable to load Registry root: " + root_response.error, {}};
  return load_document(root_url, root_response.body);
}

RegistryLoadResult
RegistryClient::load_document(const std::string &root_url,
                              const std::string &root_document) {
  try {
    const bool allow_file = root_url.rfind("file://", 0) == 0;
    if (!valid_transport_url(root_url, allow_file))
      throw RegistryError("Registry root URL must use HTTPS");
    const auto base = registry_base(root_url);
    if (root_document.size() > kMaximumRootSize)
      throw RegistryError("Registry root exceeds the size limit");
    const auto parsed_root = parse_document(root_document, "root document");
    const auto &root = object_value(parsed_root, "root document");
    require_protocol(root);
    const auto snapshot_id = required_string(root, "snapshot_id");
    const auto generated_at = required_string(root, "generated_at");
    if (!lowercase_hex(snapshot_id, 40))
      throw RegistryError("Invalid Registry snapshot_id");
    const auto &index_descriptor = object_value(member(root, "index"), "index");
    const auto index_url = required_string(index_descriptor, "url");
    const auto index_sha256 = required_string(index_descriptor, "sha256");
    const auto snapshot_base = base + "/v1/snapshots/" + snapshot_id;
    if (!same_registry_url(index_url, snapshot_base + "/index.json"))
      throw RegistryError("Registry index URL does not match snapshot_id");
    if (!lowercase_hex(index_sha256, 64))
      throw RegistryError("Invalid Registry index SHA-256");

    const auto index_response =
        http_.get(index_url, kMaximumIndexSize, allow_file);
    if (!index_response)
      throw RegistryError("Unable to load Registry index: " +
                          index_response.error);
    if (sha256(index_response.body) != index_sha256)
      throw RegistryError("Registry index SHA-256 mismatch");
    const auto index_document = parse_document(index_response.body, "index");
    const auto &index = object_value(index_document, "index");
    require_protocol(index);
    if (required_string(index, "snapshot_id") != snapshot_id)
      throw RegistryError("Registry index snapshot_id mismatch");
    if (required_string(index, "generated_at") != generated_at)
      throw RegistryError("Registry index generated_at mismatch");
    const auto &index_apps = array_value(member(index, "apps"), "apps");
    if (index_apps.size() > kMaximumApps)
      throw RegistryError("Registry app count exceeds the client limit");

    RegistryCatalog catalog{snapshot_id, generated_at, {}};
    std::set<std::string> app_ids;
    std::set<std::string> packages;
    for (const auto &item : index_apps) {
      const auto summary = parse_index_app(item, allow_file);
      const auto expected_detail =
          snapshot_base + "/apps/" + summary.app_id + ".json";
      if (!same_registry_url(summary.detail_url, expected_detail))
        throw RegistryError("App detail URL is outside the current snapshot");
      if (!app_ids.insert(summary.app_id).second ||
          !packages.insert(summary.package).second)
        throw RegistryError("Registry index contains a duplicate app");
      const auto detail_response =
          http_.get(summary.detail_url, kMaximumDetailSize, allow_file);
      if (!detail_response)
        throw RegistryError("Unable to load details for " + summary.app_id +
                            ": " + detail_response.error);
      auto detail = parse_detail(detail_response.body, allow_file);
      if (detail.app_id != summary.app_id ||
          detail.package != summary.package || detail.title != summary.title ||
          detail.summary != summary.summary ||
          detail.categories != summary.categories ||
          detail.icon_url != summary.icon_url)
        throw RegistryError("App detail does not match Registry index: " +
                            summary.app_id);
      const auto current =
          std::find_if(detail.releases.cbegin(), detail.releases.cend(),
                       [&](const auto &release) {
                         return release.status == "published" &&
                                release.version == summary.latest_version;
                       });
      if (current == detail.releases.cend())
        throw RegistryError(
            "App detail is missing the indexed published release: " +
            summary.app_id);
      detail.latest_version = summary.latest_version;
      catalog.apps.push_back(std::move(detail));
    }
    return {std::move(catalog), {}, root_document};
  } catch (const RegistryError &error) {
    return {{}, error.what(), {}};
  }
}
