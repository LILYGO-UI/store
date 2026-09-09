#ifndef LILYGO_UI_STORE_APT_INSTALLER_HPP
#define LILYGO_UI_STORE_APT_INSTALLER_HPP

#include "system/process.hpp"

#include <filesystem>
#include <string>

struct AptInstallRequest {
    std::string package;
    std::string version;
    std::string sha256;
    std::filesystem::path path;
};

struct AptInstallResult {
    std::string error;
    bool launcher_update_queued = false;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error.empty();
    }
};

[[nodiscard]] AptInstallResult install_debian_package(ProcessRunner &runner, const AptInstallRequest &request);

#endif
