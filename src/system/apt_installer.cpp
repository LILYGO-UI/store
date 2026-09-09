#include "system/apt_installer.hpp"

#include "registry/sha256.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

constexpr std::string_view kLauncherPackage = "lilygo-ui-launcher";
constexpr std::size_t kCommandOutputLimit   = 16U * 1024U * 1024U;

class FileDescriptor {
public:
    explicit FileDescriptor(int value) noexcept : value_(value)
    {
    }
    ~FileDescriptor()
    {
        if (value_ >= 0) close(value_);
    }
    FileDescriptor(const FileDescriptor &)            = delete;
    FileDescriptor &operator=(const FileDescriptor &) = delete;
    [[nodiscard]] int get() const noexcept
    {
        return value_;
    }

private:
    int value_;
};

class StagedPackage {
public:
    ~StagedPackage()
    {
        if (!path_.empty()) unlink(path_.c_str());
        if (!directory_.empty()) rmdir(directory_.c_str());
    }

    [[nodiscard]] std::string copy_from(const std::filesystem::path &source)
    {
        FileDescriptor input(open(source.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
        if (input.get() < 0) return system_error("Unable to open downloaded package");
        struct stat status{};
        if (fstat(input.get(), &status) != 0) return system_error("Unable to inspect downloaded package");
        if (!S_ISREG(status.st_mode)) return "Downloaded package must be a regular file";

        char pattern[]      = "/tmp/lilygo-ui-store-install-XXXXXX";
        const auto *created = mkdtemp(pattern);
        if (!created) return system_error("Unable to create protected package directory");
        directory_ = created;
        path_      = directory_ / "package.deb";

        FileDescriptor output(open(path_.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600));
        if (output.get() < 0) return system_error("Unable to create staged package");
        std::array<char, 64U * 1024U> buffer{};
        for (;;) {
            const auto count = read(input.get(), buffer.data(), buffer.size());
            if (count < 0) {
                if (errno == EINTR) continue;
                return system_error("Unable to read downloaded package");
            }
            if (count == 0) break;
            std::size_t offset = 0;
            while (offset < static_cast<std::size_t>(count)) {
                const auto written =
                    write(output.get(), buffer.data() + offset, static_cast<std::size_t>(count) - offset);
                if (written < 0 && errno == EINTR) continue;
                if (written <= 0) return system_error("Unable to write staged package");
                offset += static_cast<std::size_t>(written);
            }
        }

        return {};
    }

    [[nodiscard]] std::string allow_apt_reading()
    {
        // APT drops download privileges to _apt, which needs to read the local
        // .deb.
        if (chmod(path_.c_str(), 0644) != 0 || chmod(directory_.c_str(), 0755) != 0)
            return system_error("Unable to set staged package permissions");
        return {};
    }

    [[nodiscard]] const std::filesystem::path &path() const noexcept
    {
        return path_;
    }

private:
    static std::string system_error(const char *context)
    {
        return std::string(context) + ": " + std::strerror(errno);
    }

    std::filesystem::path directory_;
    std::filesystem::path path_;
};

bool valid_package_name(const std::string &value)
{
    constexpr std::string_view prefix = "lilygo-ui-";
    if (value.rfind(prefix, 0) != 0 || value.size() == prefix.size()) return false;
    bool after_separator = true;
    for (std::size_t index = prefix.size(); index < value.size(); ++index) {
        const char character = value[index];
        if (character == '-') {
            if (after_separator) return false;
            after_separator = true;
        } else if ((character >= 'a' && character <= 'z') || (character >= '0' && character <= '9')) {
            after_separator = false;
        } else {
            return false;
        }
    }
    return !after_separator;
}

bool valid_version(const std::string &value)
{
    return !value.empty() && value.front() >= '0' && value.front() <= '9' &&
           std::all_of(value.begin(), value.end(), [](unsigned char character) {
               return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                      (character >= '0' && character <= '9') || character == '.' || character == '+' ||
                      character == '-' || character == ':' || character == '~';
           });
}

bool valid_digest(const std::string &value)
{
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](unsigned char character) {
               return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
           });
}

std::string trimmed(std::string value)
{
    while (!value.empty() &&
           (value.back() == '\n' || value.back() == '\r' || value.back() == ' ' || value.back() == '\t'))
        value.pop_back();
    return value;
}

std::string command_error(const ProcessResult &result, const char *context)
{
    std::string error = context;
    error += " (exit " + std::to_string(result.exit_code) + ")";
    if (result.output_limit_exceeded) error += ": command output was truncated";
    const auto diagnostic_tail = [](const std::string &value) {
        constexpr std::size_t limit = 32U * 1024U;
        return trimmed(value.size() > limit ? "[Earlier output omitted]\n" + value.substr(value.size() - limit)
                                            : value);
    };
    const auto output = diagnostic_tail(result.output);
    const auto detail = diagnostic_tail(result.error);
    if (!output.empty()) error += "\n" + output;
    if (!detail.empty()) error += "\n" + detail;
    return error;
}

bool succeeded(const ProcessResult &result)
{
    return result.exit_code == 0 && !result.output_limit_exceeded;
}

std::vector<std::string> apt_operation(const char *operation)
{
    return {"/usr/bin/apt-get",
            "--yes",
            "--no-remove",
            "--no-install-recommends",
            "-o",
            "DPkg::Lock::Timeout=60",
            "-o",
            "Dpkg::Options::=--force-confold",
            "-o",
            "Dpkg::Use-Pty=0",
            operation,
            "--"};
}

}  // namespace

AptInstallResult install_debian_package(ProcessRunner &runner, const AptInstallRequest &request)
{
    if (!valid_package_name(request.package) || !valid_version(request.version) || !valid_digest(request.sha256) ||
        !request.path.is_absolute())
        return {"Invalid package installation request"};

    StagedPackage staged;
    if (const auto error = staged.copy_from(request.path); !error.empty()) return {error};
    std::string digest;
    std::string digest_error;
    if (!sha256_file(staged.path(), digest, digest_error)) return {"Unable to verify staged package: " + digest_error};
    if (digest != request.sha256) return {"Downloaded package SHA-256 mismatch"};
    if (const auto error = staged.allow_apt_reading(); !error.empty()) return {error};

    const auto metadata =
        runner.run({"/usr/bin/dpkg-deb", "--show", "--showformat=${Package}\t${Version}\t${Architecture}\n", "--",
                    staged.path().string()});
    if (!succeeded(metadata)) return {command_error(metadata, "Unable to read package metadata")};
    const auto fields      = trimmed(metadata.output);
    const auto package_end = fields.find('\t');
    const auto version_end = package_end == std::string::npos ? std::string::npos : fields.find('\t', package_end + 1);
    if (package_end == std::string::npos || version_end == std::string::npos ||
        fields.find_first_of("\t\r\n", version_end + 1) != std::string::npos ||
        fields.substr(0, package_end) != request.package ||
        fields.substr(package_end + 1, version_end - package_end - 1) != request.version)
        return {"Package metadata does not match the requested package and version"};
    const auto package_architecture = fields.substr(version_end + 1);
    const auto architecture         = runner.run({"/usr/bin/dpkg", "--print-architecture"});
    if (!succeeded(architecture)) return {command_error(architecture, "Unable to determine device architecture")};
    const auto native_architecture = trimmed(architecture.output);
    if (native_architecture.empty() || package_architecture.empty() ||
        (package_architecture != "all" && package_architecture != native_architecture))
        return {"Package architecture does not match the device"};

    const bool launcher = request.package == kLauncherPackage;
    auto operation      = apt_operation(launcher ? "satisfy" : "install");
    if (launcher) {
        const auto installed = runner.run(
            {"/usr/bin/dpkg-query", "--show", "--showformat=${db:Status-Abbrev}\t${Version}\n", request.package});
        if (!succeeded(installed)) return {command_error(installed, "Unable to read installed Launcher state")};
        const auto state             = trimmed(installed.output);
        const auto separator         = state.find('\t');
        const auto installed_version = separator == std::string::npos ? std::string{} : state.substr(separator + 1);
        if (separator != 3 || state[1] != 'i' || state[2] != ' ' || !valid_version(installed_version))
            return {"A fully installed Launcher with Store update support is required"};
        const auto upgrade =
            runner.run({"/usr/bin/dpkg", "--compare-versions", installed_version, "lt", request.version});
        if (!succeeded(upgrade))
            return {command_error(upgrade, "Launcher target must be newer than the installed version")};

        // Keep Launcher running at its current version until its update helper
        // exits the session. APT parses the dependency relationships, including
        // alternatives.
        operation.push_back(request.package + " (= " + installed_version + ")");
        for (const auto *field : {"Pre-Depends", "Depends"}) {
            const auto dependencies = runner.run({"/usr/bin/dpkg-deb", "--field", staged.path().string(), field});
            if (!succeeded(dependencies)) return {command_error(dependencies, "Unable to read Launcher dependencies")};
            if (!trimmed(dependencies.output).empty()) operation.push_back(dependencies.output);
        }
        // Reject incompatible installed packages before the deferred dpkg worker
        // runs. Breaks must also be resolved without deconfiguring live packages.
        for (const auto *field : {"Conflicts", "Breaks"}) {
            const auto conflicts = runner.run({"/usr/bin/dpkg-deb", "--field", staged.path().string(), field});
            if (!succeeded(conflicts)) return {command_error(conflicts, "Unable to read Launcher package conflicts")};
            if (!trimmed(conflicts.output).empty()) operation.push_back("Conflicts: " + conflicts.output);
        }
    } else {
        operation.push_back(staged.path().string());
    }

    const auto refreshed =
        runner.run({"/usr/bin/apt-get", "-o", "APT::Update::Error-Mode=any", "-o", "DPkg::Lock::Timeout=60", "update"},
                   kCommandOutputLimit);
    if (refreshed.exit_code != 0) return {command_error(refreshed, "Unable to refresh configured APT sources")};

    const auto installed = runner.run(operation, kCommandOutputLimit);
    if (installed.exit_code != 0)
        return {command_error(
            installed, launcher ? "Unable to install Launcher dependencies" : "APT package installation failed")};
    if (launcher) {
        const auto queued = runner.run({"/usr/lib/lilygo-ui-launcher/lilygo-ui-launcher-update", "queue",
                                        staged.path().string(), request.version, request.sha256},
                                       kCommandOutputLimit);
        if (queued.exit_code != 0) return {command_error(queued, "Launcher update handoff failed")};
    }
    return {{}, launcher};
}
