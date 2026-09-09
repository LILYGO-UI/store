#ifndef LILYGO_UI_STORE_PACKAGE_MANAGER_HPP
#define LILYGO_UI_STORE_PACKAGE_MANAGER_HPP

#include "registry/http_client.hpp"
#include "registry/registry_client.hpp"
#include "system/process.hpp"

#include <string>

struct InstalledPackageResult {
    bool installed = false;
    std::string version;
    std::string error;
};

struct PackageOperationResult {
    std::string installed_version;
    std::string error;
    bool launcher_update_queued = false;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error.empty();
    }
};

class PackageManager {
public:
    PackageManager(ProcessRunner &runner, HttpClient &http) noexcept : runner_(runner), http_(http)
    {
    }

    [[nodiscard]] InstalledPackageResult query(const std::string &package);
    [[nodiscard]] std::string architecture();
    [[nodiscard]] PackageOperationResult install(const std::string &package, const RegistryRelease &release);
    [[nodiscard]] PackageOperationResult remove(const std::string &package);

private:
    ProcessRunner &runner_;
    HttpClient &http_;
};

#endif
