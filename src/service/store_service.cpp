#include "service/store_service.hpp"

#include "app_identity.h"
#include "domain/debian_version.hpp"
#include "registry/root_cache.hpp"
#include "registry/sha256.hpp"

#include <png.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

std::string display_size(std::uint64_t bytes) {
  constexpr double megabyte = 1024.0 * 1024.0;
  std::ostringstream output;
  if (bytes >= static_cast<std::uint64_t>(megabyte))
    output << std::fixed << std::setprecision(1) << (bytes / megabyte) << " MB";
  else
    output << std::fixed << std::setprecision(1) << (bytes / 1024.0) << " KB";
  return output.str();
}

std::string local_id(const std::string &package) {
  constexpr std::string_view prefix = "lilygo-ui-";
  return package.rfind(prefix, 0) == 0 ? package.substr(prefix.size())
                                       : package;
}

bool official_source_repo(const std::string &source_repo) {
  std::string normalized;
  normalized.reserve(source_repo.size());
  std::transform(source_repo.cbegin(), source_repo.cend(),
                 std::back_inserter(normalized), [](unsigned char value) {
                   return static_cast<char>(std::tolower(value));
                 });
  constexpr std::string_view prefix = "https://github.com/lilygo-ui/";
  if (normalized.rfind(prefix, 0) != 0)
    return false;
  auto repository = normalized.substr(prefix.size());
  if (!repository.empty() && repository.back() == '/')
    repository.pop_back();
  constexpr std::string_view git_suffix = ".git";
  if (repository.size() > git_suffix.size() &&
      repository.compare(repository.size() - git_suffix.size(),
                         git_suffix.size(), git_suffix) == 0)
    repository.resize(repository.size() - git_suffix.size());
  return !repository.empty() && repository != "." && repository != ".." &&
         std::all_of(
             repository.cbegin(), repository.cend(), [](unsigned char c) {
               return std::isalnum(c) || c == '-' || c == '_' || c == '.';
             });
}

constexpr std::size_t kMaximumImageBytes = 4U * 1024U * 1024U;
constexpr std::uint64_t kMaximumImagePixels = 800000U;
constexpr std::uint64_t kCachedImageMaximumPixels = 240000U;
constexpr std::string_view kImageCacheVersion = "thumbnail-v1:";

struct PngDimensions {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
};

std::uint32_t read_big_endian(const unsigned char *value) {
  return (static_cast<std::uint32_t>(value[0]) << 24U) |
         (static_cast<std::uint32_t>(value[1]) << 16U) |
         (static_cast<std::uint32_t>(value[2]) << 8U) |
         static_cast<std::uint32_t>(value[3]);
}

std::uint32_t png_crc(const unsigned char *data, std::size_t size) {
  std::uint32_t crc = 0xffffffffU;
  for (std::size_t index = 0; index < size; ++index) {
    crc ^= data[index];
    for (int bit = 0; bit < 8; ++bit)
      crc = (crc >> 1U) ^ (0xedb88320U & (0U - (crc & 1U)));
  }
  return crc ^ 0xffffffffU;
}

bool valid_png_bit_depth(unsigned char bit_depth, unsigned char color_type) {
  switch (color_type) {
  case 0:
    return bit_depth == 1 || bit_depth == 2 || bit_depth == 4 ||
           bit_depth == 8 || bit_depth == 16;
  case 2:
  case 4:
  case 6:
    return bit_depth == 8 || bit_depth == 16;
  case 3:
    return bit_depth == 1 || bit_depth == 2 || bit_depth == 4 || bit_depth == 8;
  default:
    return false;
  }
}

bool valid_png_data(const unsigned char *data, std::size_t size,
                    PngDimensions *dimensions = nullptr) {
  static constexpr unsigned char signature[] = {0x89, 0x50, 0x4e, 0x47,
                                                0x0d, 0x0a, 0x1a, 0x0a};
  if (size < 45 ||
      !std::equal(std::begin(signature), std::end(signature), data))
    return false;

  std::size_t offset = sizeof(signature);
  bool saw_header = false;
  bool saw_image_data = false;
  while (offset < size) {
    if (size - offset < 12)
      return false;
    const auto length =
        static_cast<std::size_t>(read_big_endian(data + offset));
    if (length > size - offset - 12)
      return false;
    const auto *type = data + offset + 4;
    const auto *chunk_data = type + 4;
    const auto stored_crc = read_big_endian(chunk_data + length);
    if (png_crc(type, length + 4) != stored_crc)
      return false;

    const bool is_header = std::equal(type, type + 4, "IHDR");
    const bool is_image_data = std::equal(type, type + 4, "IDAT");
    const bool is_end = std::equal(type, type + 4, "IEND");
    if (!saw_header) {
      if (!is_header || length != 13)
        return false;
      const auto width = read_big_endian(chunk_data);
      const auto height = read_big_endian(chunk_data + 4);
      if (width == 0 || width > 4096 || height == 0 || height > 4096 ||
          static_cast<std::uint64_t>(width) * height > kMaximumImagePixels ||
          !valid_png_bit_depth(chunk_data[8], chunk_data[9]) ||
          chunk_data[10] != 0 || chunk_data[11] != 0 || chunk_data[12] > 1)
        return false;
      if (dimensions)
        *dimensions = {width, height};
      saw_header = true;
    } else if (is_header) {
      return false;
    }

    saw_image_data = saw_image_data || is_image_data;
    const auto next = offset + length + 12;
    if (is_end)
      return length == 0 && saw_image_data && next == size;
    offset = next;
  }
  return false;
}

bool valid_cached_png(const std::filesystem::path &path) {
  std::error_code error;
  const auto size = std::filesystem::file_size(path, error);
  if (error || size < 45 || size > kMaximumImageBytes)
    return false;
  std::ifstream input(path, std::ios::binary);
  std::vector<unsigned char> data(static_cast<std::size_t>(size));
  input.read(reinterpret_cast<char *>(data.data()),
             static_cast<std::streamsize>(data.size()));
  if (input.gcount() != static_cast<std::streamsize>(data.size()))
    return false;
  PngDimensions dimensions;
  return valid_png_data(data.data(), data.size(), &dimensions) &&
         static_cast<std::uint64_t>(dimensions.width) * dimensions.height <=
             kCachedImageMaximumPixels;
}

std::vector<unsigned char> resize_rgba(const std::vector<unsigned char> &source,
                                       const PngDimensions &source_size,
                                       const PngDimensions &destination_size) {
  std::vector<unsigned char> destination(
      static_cast<std::size_t>(destination_size.width) *
      destination_size.height * 4U);
  for (std::uint32_t y = 0; y < destination_size.height; ++y) {
    const auto source_y =
        std::clamp((static_cast<double>(y) + 0.5) * source_size.height /
                           destination_size.height -
                       0.5,
                   0.0, static_cast<double>(source_size.height - 1));
    const auto y0 = static_cast<std::uint32_t>(source_y);
    const auto y1 = std::min(y0 + 1, source_size.height - 1);
    const auto y_weight = source_y - y0;
    for (std::uint32_t x = 0; x < destination_size.width; ++x) {
      const auto source_x =
          std::clamp((static_cast<double>(x) + 0.5) * source_size.width /
                             destination_size.width -
                         0.5,
                     0.0, static_cast<double>(source_size.width - 1));
      const auto x0 = static_cast<std::uint32_t>(source_x);
      const auto x1 = std::min(x0 + 1, source_size.width - 1);
      const auto x_weight = source_x - x0;
      for (std::size_t channel = 0; channel < 4; ++channel) {
        const auto top_left =
            source[(static_cast<std::size_t>(y0) * source_size.width + x0) *
                       4U +
                   channel];
        const auto top_right =
            source[(static_cast<std::size_t>(y0) * source_size.width + x1) *
                       4U +
                   channel];
        const auto bottom_left =
            source[(static_cast<std::size_t>(y1) * source_size.width + x0) *
                       4U +
                   channel];
        const auto bottom_right =
            source[(static_cast<std::size_t>(y1) * source_size.width + x1) *
                       4U +
                   channel];
        const auto top = top_left + (top_right - top_left) * x_weight;
        const auto bottom =
            bottom_left + (bottom_right - bottom_left) * x_weight;
        destination[(static_cast<std::size_t>(y) * destination_size.width + x) *
                        4U +
                    channel] =
            static_cast<unsigned char>(
                std::lround(top + (bottom - top) * y_weight));
      }
    }
  }
  return destination;
}

bool make_cached_png(const std::string &source,
                     const PngDimensions &source_size, std::string &cached,
                     std::string &error) {
  const auto source_pixels =
      static_cast<std::uint64_t>(source_size.width) * source_size.height;
  png_image decoded{};
  decoded.version = PNG_IMAGE_VERSION;
  if (!png_image_begin_read_from_memory(&decoded, source.data(),
                                        source.size())) {
    error = decoded.message;
    png_image_free(&decoded);
    return false;
  }
  if (decoded.width != source_size.width ||
      decoded.height != source_size.height) {
    error = "PNG dimensions changed while decoding";
    png_image_free(&decoded);
    return false;
  }
  decoded.format = PNG_FORMAT_RGBA;
  std::vector<unsigned char> pixels(PNG_IMAGE_SIZE(decoded));
  if (!png_image_finish_read(&decoded, nullptr, pixels.data(), 0, nullptr)) {
    error = decoded.message;
    png_image_free(&decoded);
    return false;
  }
  png_image_free(&decoded);

  auto destination_size = source_size;
  std::vector<unsigned char> resized;
  const unsigned char *output_pixels = pixels.data();
  if (source_pixels > kCachedImageMaximumPixels) {
    const auto scale =
        std::sqrt(static_cast<double>(kCachedImageMaximumPixels) /
                  static_cast<double>(source_pixels));
    destination_size = {
        std::max(1U, static_cast<std::uint32_t>(source_size.width * scale)),
        std::max(1U, static_cast<std::uint32_t>(source_size.height * scale))};
    while (static_cast<std::uint64_t>(destination_size.width) *
               destination_size.height >
           kCachedImageMaximumPixels) {
      if (destination_size.width >= destination_size.height)
        --destination_size.width;
      else
        --destination_size.height;
    }
    resized = resize_rgba(pixels, source_size, destination_size);
    output_pixels = resized.data();
  }

  png_image encoded{};
  encoded.version = PNG_IMAGE_VERSION;
  encoded.width = destination_size.width;
  encoded.height = destination_size.height;
  encoded.format = PNG_FORMAT_RGBA;
  png_alloc_size_t encoded_size = 0;
  if (!png_image_write_to_memory(&encoded, nullptr, &encoded_size, 0,
                                 output_pixels, 0, nullptr) ||
      encoded_size == 0) {
    error = encoded.message;
    png_image_free(&encoded);
    return false;
  }
  cached.resize(static_cast<std::size_t>(encoded_size));
  if (!png_image_write_to_memory(&encoded, cached.data(), &encoded_size, 0,
                                 output_pixels, 0, nullptr)) {
    error = encoded.message;
    cached.clear();
    png_image_free(&encoded);
    return false;
  }
  cached.resize(static_cast<std::size_t>(encoded_size));
  png_image_free(&encoded);
  return true;
}

std::filesystem::path image_cache_root(std::error_code &error) {
  std::filesystem::path root;
  if (const auto *configured = std::getenv("XDG_CACHE_HOME");
      configured && *configured &&
      std::filesystem::path(configured).is_absolute())
    root = std::filesystem::path(configured);
  else if (const auto *home = std::getenv("HOME"); home && *home)
    root = std::filesystem::path(home) / ".cache";
  else
    root = std::filesystem::temp_directory_path(error) /
           (std::string(LILYGO_UI_STORE_PACKAGE_NAME) + "-" +
            std::to_string(getuid()));
  if (error)
    return {};
  return root / LILYGO_UI_STORE_PACKAGE_NAME / "images";
}

const RegistryRelease *select_release(const RegistryApplication &app,
                                      const std::string &architecture,
                                      const std::string &appkit_version) {
  const auto found = std::find_if(
      app.releases.cbegin(), app.releases.cend(), [&](const auto &release) {
        return release.status == "published" &&
               release.architecture == architecture &&
               (appkit_version.empty() ||
                debian_version_compare(release.min_appkit_version,
                                       appkit_version) <= 0);
      });
  return found == app.releases.cend() ? nullptr : &*found;
}

std::string configured_appkit_version(PackageManager &packages) {
  if (const auto *value = std::getenv("LILYGO_UI_APPKIT_VERSION");
      value && *value)
    return value;
  const auto installed = packages.query("lilygo-ui-appkit-dev");
  return installed.installed ? installed.version : std::string{};
}

} // namespace

StoreRefreshResult StoreService::refresh(const std::string &root_url) {
  return make_result(registry_.load(root_url));
}

StoreRefreshResult StoreService::refresh(const std::string &root_url,
                                         const std::string &root_document) {
  return make_result(registry_.load_document(root_url, root_document));
}

StoreRefreshResult StoreService::make_result(RegistryLoadResult loaded) {
  if (!loaded)
    return {{}, {}, loaded.error, {}, {}};
  const auto architecture = packages_.architecture();
  if (architecture.empty())
    return {
        {}, {}, "Unable to determine the device package architecture", {}, {}};
  const auto appkit_version = configured_appkit_version(packages_);

  StoreRefreshResult result;
  result.snapshot_id = loaded.catalog.snapshot_id;
  result.root_document = std::move(loaded.root_document);
  result.apps.reserve(loaded.catalog.apps.size());
  for (const auto &registry_app : loaded.catalog.apps) {
    StoreApp app;
    app.id = local_id(registry_app.package);
    app.app_id = registry_app.app_id;
    app.package = registry_app.package;
    app.name = registry_app.title;
    app.summary = registry_app.summary;
    app.description = registry_app.description;
    app.author = registry_app.authors.empty()
                     ? std::string{}
                     : registry_app.authors.front().name;
    app.latest_version = registry_app.latest_version;
    app.version = app.latest_version;
    app.icon_url = registry_app.icon_url;
    app.icon_cache_key = loaded.catalog.snapshot_id + ":" + app.icon_url;
    app.categories = registry_app.categories;
    app.permissions = registry_app.permissions;
    app.official = official_source_repo(registry_app.source_repo);
    app.screenshots.reserve(registry_app.screenshots.size());
    for (const auto &registry_screenshot : registry_app.screenshots) {
      StoreScreenshot screenshot;
      screenshot.url = registry_screenshot.url;
      screenshot.caption = registry_screenshot.caption;
      screenshot.cache_key =
          loaded.catalog.snapshot_id + ":" + registry_screenshot.url;
      app.screenshots.push_back(std::move(screenshot));
    }
    const auto *release =
        select_release(registry_app, architecture, appkit_version);
    app.available = release != nullptr;
    if (release) {
      app.release = *release;
      app.latest_version = release->version;
      app.version = release->version;
      app.size = display_size(release->size);
    } else {
      app.size = "Unavailable";
    }
    const auto installed = packages_.query(app.package);
    if (installed.installed) {
      app.installed = true;
      app.version = installed.version;
      app.update_available =
          release &&
          debian_version_compare(installed.version, release->version) < 0;
    }
    result.apps.push_back(std::move(app));
  }
  return result;
}

StoreRefreshResult refresh_store(const std::string &root_url) {
  PosixProcessRunner runner;
  CurlHttpClient http(runner);
  RegistryClient registry(http);
  PackageManager packages(runner, http);
  auto result = StoreService(registry, packages).refresh(root_url);
  if (result)
    result.cache_error =
        RegistryRootCache{}.store(root_url, result.root_document);
  return result;
}

StoreRefreshResult refresh_cached_store(const std::string &root_url) {
  const auto cached = RegistryRootCache{}.load(root_url);
  if (!cached)
    return {{}, {}, cached.error, {}, {}};

  PosixProcessRunner runner;
  CurlHttpClient http(runner);
  RegistryClient registry(http);
  PackageManager packages(runner, http);
  return StoreService(registry, packages).refresh(root_url, cached.document);
}

PackageOperationResult install_store_package(const std::string &package,
                                             const RegistryRelease &release) {
  PosixProcessRunner runner;
  CurlHttpClient http(runner);
  return PackageManager(runner, http).install(package, release);
}

PackageOperationResult remove_store_package(const std::string &package) {
  PosixProcessRunner runner;
  CurlHttpClient http(runner);
  return PackageManager(runner, http).remove(package);
}

StoreImageDownloadResult download_store_image(const std::string &url,
                                              const std::string &cache_key) {
  if (cache_key.empty())
    return {{}, "Image cache key is empty"};
  std::error_code error;
  const auto cache = image_cache_root(error);
  if (error || cache.empty())
    return {{}, "Unable to locate the image cache directory"};
  const auto cache_status = std::filesystem::symlink_status(cache, error);
  if (!error && std::filesystem::is_symlink(cache_status))
    return {{}, "The image cache directory is not safe"};
  error.clear();
  std::filesystem::create_directories(cache, error);
  if (error)
    return {{}, "Unable to create the image cache directory"};
  std::filesystem::permissions(cache, std::filesystem::perms::owner_all,
                               std::filesystem::perm_options::replace, error);
  if (error)
    return {{}, "Unable to secure the image cache directory"};

  const auto destination =
      cache / (sha256(std::string(kImageCacheVersion) + cache_key) + ".png");
  if (destination.string().size() >= 240)
    return {{}, "The image cache path is too long"};
  const auto destination_status =
      std::filesystem::symlink_status(destination, error);
  if (!error && std::filesystem::is_regular_file(destination_status) &&
      valid_cached_png(destination))
    return {destination.string(), {}};
  error.clear();
  std::filesystem::remove(destination, error);
  error.clear();

  PosixProcessRunner runner;
  CurlHttpClient http(runner);
  const bool allow_file = url.rfind("file://", 0) == 0;
  const auto downloaded = http.get_binary(url, kMaximumImageBytes, allow_file);
  if (!downloaded)
    return {{}, "Image download failed: " + downloaded.error};
  const auto *data =
      reinterpret_cast<const unsigned char *>(downloaded.body.data());
  PngDimensions source_size;
  if (!valid_png_data(data, downloaded.body.size(), &source_size))
    return {{}, "Downloaded image is not a valid PNG"};
  std::string cached_image;
  std::string normalization_error;
  if (!make_cached_png(downloaded.body, source_size, cached_image,
                       normalization_error))
    return {{}, "Unable to prepare image for display: " + normalization_error};

  static std::atomic<std::uint64_t> temporary_sequence{0};
  const auto temporary = destination.string() + ".part-" +
                         std::to_string(getpid()) + "-" +
                         std::to_string(temporary_sequence.fetch_add(1));
  std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
  output.write(cached_image.data(),
               static_cast<std::streamsize>(cached_image.size()));
  output.close();
  if (!output) {
    std::filesystem::remove(temporary, error);
    return {{}, "Unable to write the image cache"};
  }

  std::filesystem::rename(temporary, destination, error);
  if (error) {
    std::filesystem::remove(temporary, error);
    if (valid_cached_png(destination))
      return {destination.string(), {}};
    return {{}, "Unable to publish the cached image"};
  }
  return {destination.string(), {}};
}
