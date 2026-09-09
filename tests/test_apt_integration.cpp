#include "registry/sha256.hpp"
#include "system/process.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

#ifdef __linux__
namespace {

namespace fs = std::filesystem;

void require(bool condition, const std::string &message)
{
    if (!condition) throw std::runtime_error(message);
}

void write_file(const fs::path &path, const std::string &contents)
{
    fs::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << contents;
    require(stream.good(), "Unable to write " + path.string());
}

std::string trimmed(std::string value)
{
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) value.pop_back();
    return value;
}

std::string digest_of(const fs::path &path)
{
    std::string digest;
    std::string error;
    require(sha256_file(path, digest, error), error);
    return digest;
}

struct Package {
    std::string name;
    std::string version;
    fs::path path;
};

class AptFixture {
public:
    explicit AptFixture(fs::path helper) : helper_(std::move(helper))
    {
        char pattern[]       = "/tmp/lilygo-ui-store-apt-test-XXXXXX";
        const auto directory = mkdtemp(pattern);
        require(directory != nullptr, "Unable to create test directory");
        root_ = directory;
        fs::permissions(root_, fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
                                   fs::perms::others_read | fs::perms::others_exec);
        repository_ = root_ / "repository";
        fs::create_directories(repository_);
        architecture_ = trimmed(command({"/usr/bin/dpkg", "--print-architecture"}));
    }

    ~AptFixture()
    {
        std::error_code ignored;
        if (sources_changed_) fs::remove("/etc/apt/sources.list", ignored);
        for (auto item = source_backups_.rbegin(); item != source_backups_.rend(); ++item)
            fs::rename(item->second, item->first, ignored);
        fs::remove_all(root_, ignored);
    }

    void configure_local_source()
    {
        const fs::path source_parts = "/etc/apt/sources.list.d";
        fs::create_directories(source_parts);
        backup_source("/etc/apt/sources.list");
        std::vector<fs::path> sources;
        for (const auto &entry : fs::directory_iterator(source_parts)) {
            if (entry.path().extension() == ".list" || entry.path().extension() == ".sources")
                sources.push_back(entry.path());
        }
        for (const auto &source : sources) backup_source(source);
        sources_changed_ = true;
        write_file("/etc/apt/sources.list", "deb [trusted=yes] file:" + repository_.string() + " ./\n");
    }

    Package package(const std::string &name, const std::string &version, const std::string &relations = {},
                    bool in_repository = false)
    {
        const auto directory = root_ / "build" / (name + "-" + version);
        const auto path =
            (in_repository ? repository_ : root_ / "downloads") / (name + "_" + version + "_" + architecture_ + ".deb");
        write_file(directory / "DEBIAN/control", "Package: " + name + "\nVersion: " + version +
                                                     "\nArchitecture: " + architecture_ +
                                                     "\nMaintainer: Store integration tests <test@example.test>\n"
                                                     "Section: misc\nPriority: optional\n" +
                                                     relations + "Description: Disposable Store APT test fixture\n");
        write_file(directory / payload(name).relative_path(), version + "\n");
        fs::create_directories(path.parent_path());
        command({"/usr/bin/dpkg-deb", "--build", "--root-owner-group", directory.string(), path.string()});
        Package result{name, version, path};
        if (in_repository) candidates_.push_back(result);
        return result;
    }

    void publish()
    {
        std::string index;
        for (const auto &candidate : candidates_) {
            index += trimmed(command({"/usr/bin/dpkg-deb", "--field", candidate.path.string()}));
            index += "\nFilename: ./" + candidate.path.filename().string() +
                     "\nSize: " + std::to_string(fs::file_size(candidate.path)) +
                     "\nSHA256: " + digest_of(candidate.path) + "\n\n";
        }
        write_file(repository_ / "Packages", index);
    }

    ProcessResult install(const Package &package, std::string expected_name = {}, std::string expected_version = {},
                          std::string expected_digest = {})
    {
        return runner_.run({helper_.string(), expected_name.empty() ? package.name : expected_name,
                            expected_version.empty() ? package.version : expected_version,
                            expected_digest.empty() ? digest_of(package.path) : expected_digest,
                            package.path.string()});
    }

    void install_successfully(const Package &package)
    {
        const auto result = install(package);
        require(result.exit_code == 0 && !result.output_limit_exceeded,
                "Helper failed for " + package.name + ":\n" + result.output + result.error);
    }

    void require_installed(const std::string &name, const std::string &version)
    {
        const auto result =
            runner_.run({"/usr/bin/dpkg-query", "--show", "--showformat=${db:Status-Status}\n${Version}", name});
        require(result.exit_code == 0 && result.output == "installed\n" + version,
                "Expected installed " + name + "=" + version + ", got:\n" + result.output + result.error);
        require(fs::exists(payload(name)), "Package payload is missing for " + name);
    }

    void require_absent(const std::string &name)
    {
        const auto result = runner_.run({"/usr/bin/dpkg-query", "--show", "--showformat=${Status}", name});
        require(result.exit_code != 0 || result.output.find("not-installed") != std::string::npos,
                "Failed installation unpacked or configured " + name + ": " + result.output);
        require(!fs::exists(payload(name)), "Failed installation wrote " + name);
    }

    std::string command(const std::vector<std::string> &arguments)
    {
        const auto result = runner_.run(arguments);
        require(result.exit_code == 0 && !result.output_limit_exceeded,
                "Command failed: " + arguments.front() + "\n" + result.output + result.error);
        return result.output;
    }

    [[nodiscard]] fs::path temporary_path(const std::string &filename) const
    {
        return root_ / filename;
    }

private:
    static fs::path payload(const std::string &name)
    {
        return fs::path("/usr/share/lilygo-ui-store-test") / name;
    }

    void backup_source(const fs::path &path)
    {
        if (!fs::exists(path) && !fs::is_symlink(path)) return;
        const auto backup = root_ / "original-sources" / std::to_string(source_backups_.size());
        fs::create_directories(backup.parent_path());
        fs::rename(path, backup);
        source_backups_.emplace_back(path, backup);
    }

    PosixProcessRunner runner_;
    fs::path helper_;
    fs::path root_;
    fs::path repository_;
    std::string architecture_;
    std::vector<Package> candidates_;
    std::vector<std::pair<fs::path, fs::path>> source_backups_;
    bool sources_changed_ = false;
};

void test_dependency_install_and_upgrade(AptFixture &fixture)
{
    const std::string leaf       = "lilygo-ui-store-test-leaf";
    const std::string dependency = "lilygo-ui-store-test-dependency";
    const std::string app        = "lilygo-ui-store-test-app";
    fixture.package(leaf, "1.0", {}, true);
    fixture.package(dependency, "1.0", {}, true);
    fixture.package(dependency, "2.0", "Depends: " + leaf + " (>= 1.0)\n", true);
    fixture.publish();
    const auto first = fixture.package(app, "1.0", "Depends: " + dependency + " (>= 2.0)\n");
    fixture.install_successfully(first);
    fixture.require_installed(app, "1.0");
    fixture.require_installed(dependency, "2.0");
    fixture.require_installed(leaf, "1.0");
    const auto automatic = fixture.command({"/usr/bin/apt-mark", "showauto"});
    require(automatic.find(dependency + "\n") != std::string::npos && automatic.find(leaf + "\n") != std::string::npos,
            "APT did not mark dependencies as automatically installed");

    fixture.package(leaf, "2.0", {}, true);
    fixture.package(dependency, "3.0", "Depends: " + leaf + " (>= 2.0)\n", true);
    fixture.publish();
    const auto update = fixture.package(app, "2.0", "Depends: " + dependency + " (>= 3.0)\n");
    fixture.install_successfully(update);
    fixture.require_installed(app, "2.0");
    fixture.require_installed(dependency, "3.0");
    fixture.require_installed(leaf, "2.0");
    std::cout << "PASS versioned and transitive dependencies, application upgrade\n";
}

void test_missing_dependency(AptFixture &fixture)
{
    const auto app =
        fixture.package("lilygo-ui-store-test-missing", "1.0", "Depends: lilygo-ui-store-test-unavailable (>= 9.0)\n");
    require(fixture.install(app).exit_code != 0, "Installing an unavailable dependency unexpectedly succeeded");
    fixture.require_absent(app.name);
    std::cout << "PASS missing dependency fails before unpacking\n";
}

void test_removal_rejected(AptFixture &fixture)
{
    const auto blocker = fixture.package("lilygo-ui-store-test-blocker", "1.0");
    fixture.command({"/usr/bin/dpkg", "--install", blocker.path.string()});
    const auto conflict = fixture.package("lilygo-ui-store-test-conflict", "1.0", "Conflicts: " + blocker.name + "\n");
    require(fixture.install(conflict).exit_code != 0, "Helper allowed removal of an installed package");
    fixture.require_installed(blocker.name, blocker.version);
    fixture.require_absent(conflict.name);
    std::cout << "PASS dependency resolution cannot remove installed packages\n";
}

void test_artifact_validation(AptFixture &fixture)
{
    const auto app = fixture.package("lilygo-ui-store-test-validation", "1.0");
    require(fixture.install(app, {}, {}, std::string(64, '0')).exit_code != 0, "Helper accepted an incorrect SHA-256");
    fixture.require_absent(app.name);
    require(fixture.install(app, "lilygo-ui-store-test-other").exit_code != 0,
            "Helper accepted an incorrect package identity");
    fixture.require_absent(app.name);
    require(fixture.install(app, {}, "2.0").exit_code != 0, "Helper accepted an incorrect package version");
    fixture.require_absent(app.name);
    std::cout << "PASS SHA-256, package name, and version validation\n";
}

void test_launcher_dependency_handoff(AptFixture &fixture)
{
    const std::string launcher     = "lilygo-ui-launcher";
    const std::string prerequisite = "lilygo-ui-store-test-launcher-prerequisite";
    const std::string runtime      = "lilygo-ui-store-test-launcher-runtime";
    const auto installed           = fixture.package(launcher, "1.0");
    fixture.command({"/usr/bin/dpkg", "--install", installed.path.string()});
    fixture.command({"/usr/bin/apt-mark", "hold", launcher});
    fixture.package(launcher, "9.0", {}, true);
    fixture.package(prerequisite, "2.0", {}, true);
    fixture.package(runtime, "1.0", {}, true);
    fixture.publish();

    const auto log              = fixture.temporary_path("launcher-handoff.log");
    const fs::path queue_helper = "/usr/lib/lilygo-ui-launcher/lilygo-ui-launcher-update";
    write_file(queue_helper,
               "#!/bin/sh\nset -eu\n"
               "test \"$#\" -eq 4\n"
               "test \"$1\" = queue\n"
               "test -r \"$2\"\n"
               "printf '%s\\n' \"$1\" \"$3\" \"$4\" > '" +
                   log.string() + "'\n" +
                   "/usr/bin/dpkg-query --show '--showformat=${db:Status-Status} "
                   "${Version}\\n' " +
                   launcher + " >> '" + log.string() + "'\n" +
                   "/usr/bin/dpkg-query --show '--showformat=${db:Status-Status} "
                   "${Version}\\n' " +
                   prerequisite + " >> '" + log.string() + "'\n" +
                   "/usr/bin/dpkg-query --show '--showformat=${db:Status-Status} "
                   "${Version}\\n' " +
                   runtime + " >> '" + log.string() + "'\n");
    fs::permissions(queue_helper, fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
                                      fs::perms::others_read | fs::perms::others_exec);
    const auto update = fixture.package(
        launcher, "2.0", "Pre-Depends: " + prerequisite + " (>= 2.0)\nDepends: " + runtime + " (>= 1.0)\n");
    fixture.install_successfully(update);
    fixture.require_installed(launcher, "1.0");
    fixture.require_installed(prerequisite, "2.0");
    fixture.require_installed(runtime, "1.0");
    require(fixture.command({"/usr/bin/apt-mark", "showhold"}).find(launcher + "\n") != std::string::npos,
            "Launcher hold changed during dependency preparation");
    require(fixture.command({"/usr/bin/cat", log.string()}) ==
                "queue\n2.0\n" + digest_of(update.path) + "\ninstalled 1.0\ninstalled 2.0\ninstalled 1.0\n",
            "Launcher was queued before its dependencies were configured");

    fs::remove(log);
    const auto unavailable =
        fixture.package(launcher, "3.0", "Pre-Depends: lilygo-ui-store-test-launcher-unavailable (>= 1.0)\n");
    require(fixture.install(unavailable).exit_code != 0,
            "Launcher with an unavailable prerequisite unexpectedly succeeded");
    require(!fs::exists(log), "Launcher was queued after dependency failure");
    fixture.require_installed(launcher, "1.0");

    const std::string blocker = "lilygo-ui-store-test-blocker";
    fixture.require_installed(blocker, "1.0");
    const auto conflicting = fixture.package(launcher, "4.0", "Conflicts: " + blocker + "\n");
    require(fixture.install(conflicting).exit_code != 0,
            "Launcher update requiring package removal unexpectedly succeeded");
    require(!fs::exists(log), "Launcher was queued despite package conflicts");
    fixture.require_installed(blocker, "1.0");
    fixture.require_installed(launcher, "1.0");

    const auto breaking = fixture.package(launcher, "5.0", "Breaks: " + blocker + " (<< 2.0)\n");
    require(fixture.install(breaking).exit_code != 0,
            "Launcher update breaking an installed package unexpectedly succeeded");
    require(!fs::exists(log), "Launcher was queued despite a broken package relation");
    fixture.require_installed(blocker, "1.0");
    fixture.require_installed(launcher, "1.0");
    std::cout << "PASS Launcher Pre-Depends, held installed version, and handoff "
                 "gating\n";
    std::cout << "PASS Launcher Conflicts and Breaks reject unsafe handoff\n";
}

}  // namespace
#endif

int main(int argc, char **argv)
{
#ifndef __linux__
    static_cast<void>(argc);
    static_cast<void>(argv);
    std::cerr << "APT integration tests require a disposable Linux container\n";
    return 77;
#else
    const auto enabled = std::getenv("LILYGO_UI_STORE_APT_TEST_CONTAINER");
    // These tests replace APT sources and install fixture packages as root.
    if (enabled == nullptr || std::string(enabled) != "1" || geteuid() != 0 ||
        (!fs::exists("/.dockerenv") && !fs::exists("/run/.containerenv"))) {
        std::cerr << "Refusing APT integration tests outside an explicitly enabled "
                     "disposable root container\n";
        return 77;
    }
    if (argc != 2) {
        std::cerr << "Usage: store-apt-integration-tests /absolute/path/to/helper\n";
        return 2;
    }
    try {
        const auto helper = fs::canonical(argv[1]);
        require(access(helper.c_str(), X_OK) == 0, "Helper is not executable");
        AptFixture fixture(helper);
        fixture.configure_local_source();
        test_dependency_install_and_upgrade(fixture);
        test_missing_dependency(fixture);
        test_removal_rejected(fixture);
        test_artifact_validation(fixture);
        test_launcher_dependency_handoff(fixture);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "APT integration test failed: " << error.what() << '\n';
        return 1;
    }
#endif
}
