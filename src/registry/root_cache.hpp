#ifndef LILYGO_UI_STORE_REGISTRY_ROOT_CACHE_HPP
#define LILYGO_UI_STORE_REGISTRY_ROOT_CACHE_HPP

#include <cstddef>
#include <string>
#include <vector>

struct RegistryCacheDocument {
    std::string url;
    std::string body;
};

struct RegistryRootCacheResult {
    std::string document;
    std::string error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error.empty();
    }
};

class RegistryRootCache {
public:
    [[nodiscard]] RegistryRootCacheResult load(const std::string &root_url) const;
    [[nodiscard]] RegistryRootCacheResult load_document(const std::string &root_url, const std::string &document_url,
                                                        std::size_t maximum_size) const;
    [[nodiscard]] std::string store(const std::string &root_url,
                                    const std::vector<RegistryCacheDocument> &documents) const;
};

#endif
