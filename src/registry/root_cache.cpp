#include "registry/root_cache.hpp"

#include "app_identity.h"
#include "registry/sha256.hpp"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <system_error>
#include <unistd.h>
#include <utility>

namespace {

constexpr std::size_t kMaximumRootSize      = 64U * 1024U;
constexpr std::size_t kMaximumDocumentSize  = 4U * 1024U * 1024U;
constexpr std::size_t kMaximumDocumentCount = 5002;

std::filesystem::path cache_directory(const std::string &root_url, std::error_code &error)
{
    std::filesystem::path root;
    if (const auto *configured = std::getenv("XDG_CACHE_HOME");
        configured && *configured && std::filesystem::path(configured).is_absolute()) {
        root = configured;
    } else if (const auto *home = std::getenv("HOME"); home && *home) {
        root = std::filesystem::path(home) / ".cache";
    } else {
        root = std::filesystem::temp_directory_path(error) /
               (std::string(LILYGO_UI_STORE_PACKAGE_NAME) + "-" + std::to_string(getuid()));
    }
    if (error) return {};
    return root / LILYGO_UI_STORE_PACKAGE_NAME / "registry" / sha256(root_url);
}

std::filesystem::path document_path(const std::filesystem::path &directory, const std::string &root_url,
                                    const std::string &document_url)
{
    if (document_url == root_url) return directory / "root.json";
    return directory / "documents" / (sha256(document_url) + ".json");
}

RegistryRootCacheResult read_document(const std::filesystem::path &path, std::size_t maximum_size)
{
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error || !std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status))
        return {{}, "The cached Registry document is unavailable"};
    const auto size = std::filesystem::file_size(path, error);
    if (error || size == 0 || size > maximum_size) return {{}, "The cached Registry document is invalid"};

    std::ifstream input(path, std::ios::binary);
    std::string document(static_cast<std::size_t>(size), '\0');
    input.read(document.data(), static_cast<std::streamsize>(document.size()));
    if (!input || input.gcount() != static_cast<std::streamsize>(document.size()))
        return {{}, "Unable to read the cached Registry document"};
    return {std::move(document), {}};
}

std::string prepare_directory(const std::filesystem::path &directory)
{
    std::error_code error;
    const auto status = std::filesystem::symlink_status(directory, error);
    if (!error && std::filesystem::is_symlink(status)) return "The Registry cache directory is not safe";
    error.clear();
    std::filesystem::create_directories(directory, error);
    if (error) return "Unable to create the Registry cache directory";
    std::filesystem::permissions(directory, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace,
                                 error);
    return error ? "Unable to secure the Registry cache directory" : std::string{};
}

std::string store_document(const std::filesystem::path &destination, const std::string &document)
{
    std::error_code error;
    const auto status = std::filesystem::symlink_status(destination, error);
    if (!error && std::filesystem::is_symlink(status)) return "The Registry cache file is not safe";
    error.clear();

    static std::atomic<std::uint64_t> temporary_sequence{0};
    const auto temporary = destination.string() + ".part-" + std::to_string(getpid()) + "-" +
                           std::to_string(temporary_sequence.fetch_add(1));
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
        return "Unable to publish the cached Registry document";
    }
    return {};
}

}  // namespace

RegistryRootCacheResult RegistryRootCache::load(const std::string &root_url) const
{
    return load_document(root_url, root_url, kMaximumRootSize);
}

RegistryRootCacheResult RegistryRootCache::load_document(const std::string &root_url, const std::string &document_url,
                                                         std::size_t maximum_size) const
{
    if (maximum_size == 0 || maximum_size > kMaximumDocumentSize) return {{}, "Invalid Registry cache size limit"};
    std::error_code error;
    const auto directory = cache_directory(root_url, error);
    if (error || directory.empty()) return {{}, "Unable to locate the Registry cache directory"};
    return read_document(document_path(directory, root_url, document_url), maximum_size);
}

std::string RegistryRootCache::store(const std::string &root_url,
                                     const std::vector<RegistryCacheDocument> &documents) const
{
    if (documents.empty() || documents.size() > kMaximumDocumentCount)
        return "Registry cache contains an invalid document count";

    const RegistryCacheDocument *root_document = nullptr;
    std::set<std::string> urls;
    for (const auto &document : documents) {
        const auto maximum_size = document.url == root_url ? kMaximumRootSize : kMaximumDocumentSize;
        if (document.url.empty() || document.body.empty() || document.body.size() > maximum_size ||
            !urls.insert(document.url).second)
            return "Registry cache contains an invalid document";
        if (document.url == root_url) root_document = &document;
    }
    if (!root_document) return "Registry cache is missing root.json";

    std::error_code error;
    const auto directory = cache_directory(root_url, error);
    if (error || directory.empty()) return "Unable to locate the Registry cache directory";
    if (const auto directory_error = prepare_directory(directory); !directory_error.empty()) return directory_error;
    const auto resources = directory / "documents";
    if (const auto resource_error = prepare_directory(resources); !resource_error.empty()) return resource_error;

    for (const auto &document : documents) {
        if (document.url == root_url) continue;
        const auto write_error = store_document(document_path(directory, root_url, document.url), document.body);
        if (!write_error.empty()) return write_error;
    }

    // Publish root.json last so it never points at partially written resources.
    return store_document(document_path(directory, root_url, root_url), root_document->body);
}
