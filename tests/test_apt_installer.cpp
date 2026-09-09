#include "system/apt_installer.hpp"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;

constexpr const char *kDigest         = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
constexpr const char *kLauncherHelper = "/usr/lib/lilygo-ui-launcher/lilygo-ui-launcher-update";

void write_file(const fs::path &path, const std::string &contents)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << contents;
    assert(output.good());
}

std::string read_file(const fs::path &path)
{
    std::ifstream input(path, std::ios::binary);
    assert(input.good());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

class PackageFixture {
public:
    PackageFixture()
    {
        char pattern[]       = "/tmp/lilygo-ui-store-apt-unit-XXXXXX";
        const auto directory = mkdtemp(pattern);
        assert(directory != nullptr);
        directory_ = directory;
        path       = directory_ / "download with spaces.deb";
        write_file(path, "abc");
        fs::permissions(path, fs::perms::owner_read | fs::perms::owner_write);
    }

    ~PackageFixture()
    {
        std::error_code ignored;
        fs::remove_all(directory_, ignored);
    }

    AptInstallRequest request(const std::string &package = "lilygo-ui-demo") const
    {
        return {package, "2.0.0", kDigest, path};
    }

    fs::path path;

private:
    fs::path directory_;
};

bool contains(const std::vector<std::string> &arguments, const std::string &argument)
{
    return std::find(arguments.begin(), arguments.end(), argument) != arguments.end();
}

bool contains_pair(const std::vector<std::string> &arguments, const std::string &first, const std::string &second)
{
    for (std::size_t index = 0; index + 1 < arguments.size(); ++index) {
        if (arguments[index] == first && arguments[index + 1] == second) return true;
    }
    return false;
}

struct Invocation {
    std::vector<std::string> arguments;
    std::size_t output_limit;
};

class FakeRunner final : public ProcessRunner {
public:
    explicit FakeRunner(const AptInstallRequest &request) : source(request.path)
    {
        metadata.output = request.package + "\t" + request.version + "\tarm64\n";
    }

    ProcessResult run(const std::vector<std::string> &arguments, std::size_t output_limit) override
    {
        assert(!arguments.empty());
        calls.push_back({arguments, output_limit});
        if (arguments.front() == "/usr/bin/dpkg-deb" && contains(arguments, "--show")) {
            staged = arguments.back();
            assert(staged != source);
            assert(staged.is_absolute());
            assert(staged.extension() == ".deb");
            assert(contains_pair(arguments, "--", staged.string()));
            check_staged();
            if (on_metadata) on_metadata();
            return metadata;
        }

        check_staged();
        if (arguments.front() == "/usr/bin/dpkg") {
            if (contains(arguments, "--compare-versions")) return upgrade;
            assert(contains(arguments, "--print-architecture"));
            return architecture;
        }
        if (arguments.front() == "/usr/bin/dpkg-query") return installed_launcher;
        if (arguments.front() == "/usr/bin/dpkg-deb") {
            assert(contains(arguments, "--field"));
            assert(arguments.at(2) == staged.string());
            if (arguments.back() == "Pre-Depends") return pre_depends;
            if (arguments.back() == "Depends") return depends;
            if (arguments.back() == "Conflicts") return conflicts;
            assert(arguments.back() == "Breaks");
            return breaks;
        }
        if (arguments.front() == "/usr/bin/apt-get") {
            assert(output_limit >= 1024U * 1024U);
            if (contains(arguments, "update")) return update;
            assert(contains(arguments, "install") || contains(arguments, "satisfy"));
            return operation;
        }
        assert(arguments.front() == kLauncherHelper);
        return handoff;
    }

    void check_staged() const
    {
        assert(!staged.empty());
        assert(read_file(staged) == "abc");
        struct stat file_status{};
        struct stat directory_status{};
        assert(stat(staged.c_str(), &file_status) == 0);
        assert(stat(staged.parent_path().c_str(), &directory_status) == 0);
        assert(S_ISREG(file_status.st_mode));
        assert((file_status.st_mode & 0777) == 0644);
        assert((directory_status.st_mode & 0777) == 0755);
        assert(file_status.st_uid == getuid());
        assert(directory_status.st_uid == getuid());
    }

    void check_cleaned() const
    {
        if (!staged.empty()) {
            assert(!fs::exists(staged));
            assert(!fs::exists(staged.parent_path()));
        }
    }

    std::size_t count(const std::string &executable, const std::string &argument = {}) const
    {
        return static_cast<std::size_t>(std::count_if(calls.begin(), calls.end(), [&](const Invocation &call) {
            return call.arguments.front() == executable && (argument.empty() || contains(call.arguments, argument));
        }));
    }

    const std::vector<std::string> &find(const std::string &executable, const std::string &argument = {}) const
    {
        for (const auto &call : calls) {
            if (call.arguments.front() == executable && (argument.empty() || contains(call.arguments, argument)))
                return call.arguments;
        }
        assert(false && "Expected command was not run");
        std::abort();
    }

    fs::path source;
    fs::path staged;
    std::vector<Invocation> calls;
    std::function<void()> on_metadata;
    ProcessResult metadata{0, {}, {}};
    ProcessResult architecture{0, "arm64\n", {}};
    ProcessResult installed_launcher{0, "ii \t1.0.0\n", {}};
    ProcessResult upgrade{0, {}, {}};
    ProcessResult pre_depends{0, {}, {}};
    ProcessResult depends{0, {}, {}};
    ProcessResult conflicts{0, {}, {}};
    ProcessResult breaks{0, {}, {}};
    ProcessResult update{0, {}, {}};
    ProcessResult operation{0, {}, {}};
    ProcessResult handoff{0, {}, {}};
};

void check_apt_options(const std::vector<std::string> &arguments)
{
    assert(arguments.front() == "/usr/bin/apt-get");
    assert(contains(arguments, "--yes"));
    assert(contains(arguments, "--no-remove"));
    assert(contains(arguments, "--no-install-recommends"));
    assert(contains_pair(arguments, "-o", "DPkg::Lock::Timeout=60"));
    assert(contains_pair(arguments, "-o", "Dpkg::Options::=--force-confold"));
    assert(!contains(arguments, "--allow-unauthenticated"));
    assert(!contains(arguments, "--allow-downgrades"));
    assert(!contains(arguments, "--allow-change-held-packages"));
}

void test_application_install_uses_verified_staged_package()
{
    PackageFixture fixture;
    const auto request = fixture.request();
    FakeRunner runner(request);
    runner.on_metadata = [&] { write_file(fixture.path, "changed after staging"); };
    const auto result  = install_debian_package(runner, request);
    assert(result);
    assert(!result.launcher_update_queued);
    assert(runner.count("/usr/bin/apt-get", "update") == 1);
    assert(runner.count("/usr/bin/apt-get", "install") == 1);
    assert(runner.count(kLauncherHelper) == 0);
    const auto &update = runner.find("/usr/bin/apt-get", "update");
    assert(contains_pair(update, "-o", "APT::Update::Error-Mode=any"));
    assert(contains_pair(update, "-o", "DPkg::Lock::Timeout=60"));
    const auto &install = runner.find("/usr/bin/apt-get", "install");
    check_apt_options(install);
    assert(contains_pair(install, "--", runner.staged.string()));
    assert(install.back() == runner.staged.string());
    assert(!contains(install, fixture.path.string()));
    assert(runner.calls.at(runner.calls.size() - 2).arguments == update);
    assert(runner.calls.back().arguments == install);
    runner.check_cleaned();
    assert(read_file(fixture.path) == "changed after staging");
}

void test_invalid_inputs_stop_before_commands()
{
    PackageFixture fixture;
    std::vector<AptInstallRequest> invalid;
    auto request      = fixture.request();
    request.sha256[0] = '0';
    invalid.push_back(request);
    request         = fixture.request();
    request.package = "lilygo-ui-demo;touch /tmp/invalid";
    invalid.push_back(request);
    request         = fixture.request();
    request.version = "2.0.0\nInjected: true";
    invalid.push_back(request);
    request        = fixture.request();
    request.sha256 = "invalid";
    invalid.push_back(request);
    request      = fixture.request();
    request.path = "relative.deb";
    invalid.push_back(request);
    request      = fixture.request();
    request.path = fixture.path.parent_path() / "missing.deb";
    invalid.push_back(request);
    request      = fixture.request();
    request.path = fixture.path.parent_path();
    invalid.push_back(request);
    request      = fixture.request();
    request.path = fixture.path.parent_path() / "symlink.deb";
    fs::create_symlink(fixture.path, request.path);
    invalid.push_back(request);

    for (const auto &candidate : invalid) {
        FakeRunner runner(candidate);
        const auto result = install_debian_package(runner, candidate);
        assert(!result);
        assert(!result.launcher_update_queued);
        assert(runner.calls.empty());
    }
}

void test_metadata_and_architecture_rejections()
{
    PackageFixture fixture;
    const auto request = fixture.request();
    for (const auto *metadata :
         {"lilygo-ui-other\t2.0.0\tarm64\n", "lilygo-ui-demo\t9.0.0\tarm64\n", "lilygo-ui-demo\t2.0.0\tamd64\n",
          "lilygo-ui-demo\t2.0.0\t\n", "lilygo-ui-demo\t2.0.0\tarm64\nextra", "malformed metadata"}) {
        FakeRunner runner(request);
        runner.metadata.output = metadata;
        const auto result      = install_debian_package(runner, request);
        assert(!result);
        assert(runner.count("/usr/bin/apt-get") == 0);
        runner.check_cleaned();
    }
    FakeRunner runner(request);
    runner.metadata.output = "lilygo-ui-demo\t2.0.0\tall\n";
    assert(install_debian_package(runner, request));
    runner.check_cleaned();
}

void test_source_refresh_failure_prevents_install()
{
    PackageFixture fixture;
    for (const auto *package : {"lilygo-ui-demo", "lilygo-ui-launcher"}) {
        const auto request = fixture.request(package);
        FakeRunner runner(request);
        runner.update     = {100, "Reading package lists...\n", "E: Malformed entry in configured source list\n"};
        const auto result = install_debian_package(runner, request);
        assert(!result);
        assert(!result.launcher_update_queued);
        assert(result.error.find("refresh configured APT sources") != std::string::npos);
        assert(result.error.find("Malformed entry") != std::string::npos);
        assert(result.error.find("Reading package lists") != std::string::npos);
        assert(runner.count("/usr/bin/apt-get") == 1);
        assert(runner.count(kLauncherHelper) == 0);
        runner.check_cleaned();
    }
}

void test_dependency_failure_is_reported_without_handoff()
{
    PackageFixture fixture;
    for (const auto *package : {"lilygo-ui-demo", "lilygo-ui-launcher"}) {
        const auto request = fixture.request(package);
        FakeRunner runner(request);
        runner.operation  = {100, "libexample : Depends: missing-provider\n", "E: Unable to correct problems\n"};
        const auto result = install_debian_package(runner, request);
        assert(!result);
        assert(!result.launcher_update_queued);
        assert(result.error.find("missing-provider") != std::string::npos);
        assert(result.error.find("Unable to correct problems") != std::string::npos);
        assert(runner.count(kLauncherHelper) == 0);
        runner.check_cleaned();
    }
}

void test_launcher_dependencies_are_delegated_to_apt_unchanged()
{
    PackageFixture fixture;
    auto request    = fixture.request("lilygo-ui-launcher");
    request.version = "1:2.0.0-1";
    FakeRunner runner(request);
    runner.installed_launcher.output = "ii \t1:1.0.0-2+device1\n";
    runner.pre_depends.output        = "pre-provider (>= 1:2.0~rc1) | alternative\n";
    runner.depends.output =
        "libexample:any (>= 2.0), virtual-provider | fallback-provider,\n"
        " architecture-provider:arm64 (<< 3.0)\n";
    runner.conflicts.output = "conflicting-provider (<< 3.0), other-conflict\n";
    runner.breaks.output    = "obsolete-provider (<= 2.0)\n";
    const auto result       = install_debian_package(runner, request);
    assert(result);
    assert(result.launcher_update_queued);
    assert(runner.count("/usr/bin/apt-get", "install") == 0);
    assert(
        (runner.find("/usr/bin/dpkg", "--compare-versions") ==
         std::vector<std::string>{"/usr/bin/dpkg", "--compare-versions", "1:1.0.0-2+device1", "lt", request.version}));
    const auto &satisfy = runner.find("/usr/bin/apt-get", "satisfy");
    check_apt_options(satisfy);
    const auto separator = std::find(satisfy.begin(), satisfy.end(), "--");
    assert(separator != satisfy.end());
    assert((std::vector<std::string>(separator + 1, satisfy.end()) ==
            std::vector<std::string>{"lilygo-ui-launcher (= 1:1.0.0-2+device1)", runner.pre_depends.output,
                                     runner.depends.output, "Conflicts: " + runner.conflicts.output,
                                     "Conflicts: " + runner.breaks.output}));
    const auto &handoff = runner.find(kLauncherHelper);
    assert((handoff == std::vector<std::string>{kLauncherHelper, "queue", runner.staged.string(), request.version,
                                                request.sha256}));
    assert(runner.calls.at(runner.calls.size() - 2).arguments == satisfy);
    assert(runner.calls.back().arguments == handoff);
    runner.check_cleaned();
}

void test_launcher_without_dependencies_keeps_installed_version()
{
    PackageFixture fixture;
    const auto request = fixture.request("lilygo-ui-launcher");
    FakeRunner runner(request);
    runner.pre_depends.output = "\n";
    runner.depends.output     = " \t\r\n";
    const auto result         = install_debian_package(runner, request);
    assert(result);
    assert(result.launcher_update_queued);
    const auto &satisfy = runner.find("/usr/bin/apt-get", "satisfy");
    assert(contains_pair(satisfy, "--", "lilygo-ui-launcher (= 1.0.0)"));
    assert(satisfy.back() == "lilygo-ui-launcher (= 1.0.0)");
    runner.check_cleaned();
}

void test_launcher_requires_fully_installed_current_version()
{
    PackageFixture fixture;
    const auto request = fixture.request("lilygo-ui-launcher");
    for (const auto *status :
         {"iU \t1.0.0\n", "rc \t1.0.0\n", "iiR\t1.0.0\n", "ii \t\n", "ii \t1.0.0\nextra", "malformed"}) {
        FakeRunner runner(request);
        runner.installed_launcher.output = status;
        const auto result                = install_debian_package(runner, request);
        assert(!result);
        assert(runner.count("/usr/bin/apt-get") == 0);
        assert(runner.count(kLauncherHelper) == 0);
        runner.check_cleaned();
    }
}

void test_held_launcher_can_update_with_current_version_constraint()
{
    PackageFixture fixture;
    const auto request = fixture.request("lilygo-ui-launcher");
    FakeRunner runner(request);
    runner.installed_launcher.output = "hi \t1.0.0\n";
    const auto result                = install_debian_package(runner, request);
    assert(result);
    assert(result.launcher_update_queued);
    const auto &satisfy = runner.find("/usr/bin/apt-get", "satisfy");
    assert(contains_pair(satisfy, "--", "lilygo-ui-launcher (= 1.0.0)"));
    assert(!contains(satisfy, "--allow-change-held-packages"));
    runner.check_cleaned();
}

void test_subprocess_failures_stop_at_failed_command()
{
    PackageFixture fixture;
    const auto request                                     = fixture.request("lilygo-ui-launcher");
    const std::vector<ProcessResult FakeRunner::*> results = {
        &FakeRunner::metadata,  &FakeRunner::architecture, &FakeRunner::installed_launcher,
        &FakeRunner::upgrade,   &FakeRunner::pre_depends,  &FakeRunner::depends,
        &FakeRunner::conflicts, &FakeRunner::breaks,       &FakeRunner::update,
        &FakeRunner::operation, &FakeRunner::handoff};
    for (std::size_t index = 0; index < results.size(); ++index) {
        for (const auto output_limit_exceeded : {false, true}) {
            FakeRunner runner(request);
            auto &failed           = runner.*results[index];
            const bool transaction = results[index] == &FakeRunner::update ||
                                     results[index] == &FakeRunner::operation || results[index] == &FakeRunner::handoff;
            failed.exit_code             = output_limit_exceeded && !transaction ? 0 : 1;
            failed.output_limit_exceeded = output_limit_exceeded;
            failed.error                 = "subprocess error detail";
            const auto result            = install_debian_package(runner, request);
            assert(!result);
            assert(!result.launcher_update_queued);
            assert(result.error.find("subprocess error detail") != std::string::npos);
            if (output_limit_exceeded) assert(result.error.find("output was truncated") != std::string::npos);
            assert(runner.calls.size() == index + 1);
            runner.check_cleaned();
        }
    }
}

void test_launcher_conflicts_prevent_handoff()
{
    PackageFixture fixture;
    const auto request = fixture.request("lilygo-ui-launcher");
    for (const auto relation : {&FakeRunner::conflicts, &FakeRunner::breaks}) {
        FakeRunner runner(request);
        (runner.*relation).output = "installed-provider (<< 3.0)\n";
        runner.operation          = {100, {}, "E: Packages need to be removed but remove is disabled\n"};
        const auto result         = install_debian_package(runner, request);
        assert(!result);
        assert(!result.launcher_update_queued);
        assert(result.error.find("remove is disabled") != std::string::npos);
        const auto &satisfy = runner.find("/usr/bin/apt-get", "satisfy");
        assert(contains(satisfy, "Conflicts: " + (runner.*relation).output));
        assert(contains(satisfy, "--no-remove"));
        assert(runner.count(kLauncherHelper) == 0);
        runner.check_cleaned();
    }
}

void test_launcher_rejects_non_upgrade_before_apt()
{
    PackageFixture fixture;
    const auto request = fixture.request("lilygo-ui-launcher");
    for (const auto *version : {"2.0.0", "3.0.0"}) {
        FakeRunner runner(request);
        runner.installed_launcher.output = std::string("ii \t") + version + "\n";
        runner.upgrade.exit_code         = 1;
        const auto result                = install_debian_package(runner, request);
        assert(!result);
        assert(result.error.find("must be newer") != std::string::npos);
        assert(runner.count("/usr/bin/apt-get") == 0);
        assert(runner.count(kLauncherHelper) == 0);
        runner.check_cleaned();
    }
}

void test_truncated_transaction_output_still_reports_success()
{
    PackageFixture fixture;
    for (const auto *package : {"lilygo-ui-demo", "lilygo-ui-launcher"}) {
        const auto request = fixture.request(package);
        FakeRunner runner(request);
        runner.update.output_limit_exceeded    = true;
        runner.operation.output_limit_exceeded = true;
        runner.handoff.output_limit_exceeded   = true;
        const auto result                      = install_debian_package(runner, request);
        assert(result);
        assert(result.launcher_update_queued == (request.package == "lilygo-ui-launcher"));
        runner.check_cleaned();
    }
}

}  // namespace

int main()
{
    test_application_install_uses_verified_staged_package();
    test_invalid_inputs_stop_before_commands();
    test_metadata_and_architecture_rejections();
    test_source_refresh_failure_prevents_install();
    test_dependency_failure_is_reported_without_handoff();
    test_launcher_dependencies_are_delegated_to_apt_unchanged();
    test_launcher_without_dependencies_keeps_installed_version();
    test_launcher_requires_fully_installed_current_version();
    test_held_launcher_can_update_with_current_version_constraint();
    test_subprocess_failures_stop_at_failed_command();
    test_launcher_conflicts_prevent_handoff();
    test_launcher_rejects_non_upgrade_before_apt();
    test_truncated_transaction_output_still_reports_success();
    return 0;
}
