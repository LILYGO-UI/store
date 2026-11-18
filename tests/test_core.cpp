#include "app_identity.h"
#include "app_router.hpp"
#include "domain/debian_version.hpp"
#include "domain/store_model.hpp"
#include "registry/http_client.hpp"
#include "registry/json.hpp"
#include "registry/registry_client.hpp"
#include "registry/root_cache.hpp"
#include "registry/sha256.hpp"
#include "service/store_service.hpp"
#include "system/package_manager.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

constexpr const char *kBase     = "https://registry.test";
constexpr const char *kSnapshot = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

struct Fixture {
    std::string root_url;
    std::map<std::string, std::string> documents;
};

class ScopedEnvironmentVariable {
public:
    ScopedEnvironmentVariable(const char *name, const char *value) : name_(name)
    {
        const auto *previous = std::getenv(name);
        had_previous_        = previous != nullptr;
        if (previous) previous_ = previous;
        assert((value ? setenv(name, value, 1) : unsetenv(name)) == 0);
    }

    ~ScopedEnvironmentVariable()
    {
        assert((had_previous_ ? setenv(name_.c_str(), previous_.c_str(), 1) : unsetenv(name_.c_str())) == 0);
    }

    ScopedEnvironmentVariable(const ScopedEnvironmentVariable &)            = delete;
    ScopedEnvironmentVariable &operator=(const ScopedEnvironmentVariable &) = delete;

private:
    std::string name_;
    std::string previous_;
    bool had_previous_ = false;
};

std::string file_url(const std::filesystem::path &path)
{
    return "file://" + std::filesystem::absolute(path).string();
}

std::string read_binary(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    assert(input);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write_binary(const std::filesystem::path &path, const std::string &value)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(value.data(), static_cast<std::streamsize>(value.size()));
    assert(output.good());
}

std::uint32_t read_png_u32(const std::string &png, std::size_t offset)
{
    assert(offset + 4 <= png.size());
    const auto *data = reinterpret_cast<const unsigned char *>(png.data() + offset);
    return (static_cast<std::uint32_t>(data[0]) << 24U) | (static_cast<std::uint32_t>(data[1]) << 16U) |
           (static_cast<std::uint32_t>(data[2]) << 8U) | static_cast<std::uint32_t>(data[3]);
}

void write_png_u32(std::string &png, std::size_t offset, std::uint32_t value)
{
    assert(offset + 4 <= png.size());
    png[offset]     = static_cast<char>((value >> 24U) & 0xffU);
    png[offset + 1] = static_cast<char>((value >> 16U) & 0xffU);
    png[offset + 2] = static_cast<char>((value >> 8U) & 0xffU);
    png[offset + 3] = static_cast<char>(value & 0xffU);
}

std::uint32_t png_crc(const std::string &png, std::size_t offset, std::size_t size)
{
    assert(offset + size <= png.size());
    std::uint32_t crc = 0xffffffffU;
    for (std::size_t index = offset; index < offset + size; ++index) {
        crc ^= static_cast<unsigned char>(png[index]);
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1U) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return crc ^ 0xffffffffU;
}

std::size_t find_png_chunk(const std::string &png, const char *type)
{
    constexpr std::size_t signature_size = 8;
    std::size_t offset                   = signature_size;
    while (offset <= png.size() && png.size() - offset >= 12) {
        const auto length = static_cast<std::size_t>(read_png_u32(png, offset));
        if (length > png.size() - offset - 12) return std::string::npos;
        if (png.compare(offset + 4, 4, type) == 0) return offset;
        offset += length + 12;
    }
    return std::string::npos;
}

bool has_argument_pair(const std::vector<std::string> &arguments, const std::string &first, const std::string &second)
{
    for (std::size_t index = 0; index + 1 < arguments.size(); ++index) {
        if (arguments[index] == first && arguments[index + 1] == second) return true;
    }
    return false;
}

RegistryRelease make_registry_release(const std::string &version = "1.2.3", const std::string &architecture = "arm64",
                                      const std::string &status             = "published",
                                      const std::string &min_appkit_version = "0.1.0")
{
    return {version,
            architecture,
            status,
            7,
            "239f59ed55e737c77147cf55ad0c1b030b6d7ee748a7426952f9b852d5a935e5",
            "https://registry.test/packages/demo_" + version + "_" + architecture + ".deb",
            min_appkit_version,
            "2026-08-31T12:00:00+08:00",
            status == "yanked" ? "Withdrawn test release" : ""};
}

Fixture make_registry_fixture(const std::string &source_repo               = "https://github.com/LILYGO-UI/demo",
                              const std::vector<RegistryRelease> &releases = {make_registry_release()})
{
    std::string release_documents;
    std::string latest_version;
    for (const auto &release : releases) {
        if (!release_documents.empty()) release_documents += ',';
        release_documents += "{\"version\":\"" + release.version + "\",\"architecture\":\"" + release.architecture +
                             "\",\"status\":\"" + release.status + "\",\"size\":" + std::to_string(release.size) +
                             ",\"sha256\":\"" + release.sha256 + "\",\"url\":\"" + release.url +
                             "\",\"min_appkit_version\":\"" + release.min_appkit_version + "\",\"published_at\":\"" +
                             release.published_at + "\",\"reason\":\"" + release.reason + "\"}";
        if (release.status == "published" &&
            (latest_version.empty() || debian_version_compare(release.version, latest_version) > 0))
            latest_version = release.version;
    }
    const std::string detail_url = std::string(kBase) + "/v1/snapshots/" + kSnapshot + "/apps/cc.lilygo.ui.Demo.json";
    const std::string icon_url   = std::string(kBase) + "/assets/demo.png";
    const std::string detail =
        "{"
        "\"protocol_version\":1,"
        "\"app_id\":\"cc.lilygo.ui.Demo\","
        "\"package\":\"lilygo-ui-demo\","
        "\"title\":\"演示\","
        "\"summary\":\"Registry 测试应用\","
        "\"description\":\"来自不可变 Registry snapshot。\","
        "\"authors\":[{\"name\":\"Alice\",\"github\":\"alice\"}],"
        "\"categories\":[\"Utilities\"],"
        "\"license\":\"MIT\","
        "\"source_repo\":\"" +
        source_repo +
        "\","
        "\"homepage\":\"https://example.com/demo\","
        "\"icon_url\":\"" +
        icon_url +
        "\","
        "\"screenshots\":[{\"url\":\"https://registry.test/assets/screen.png\","
        "\"caption\":\"主界面\"}],"
        "\"permissions\":[{\"id\":\"network\",\"reason\":\"获取在线数据\"}],"
        "\"releases\":[" +
        release_documents + "]}";
    const std::string index =
        "{"
        "\"protocol_version\":1,"
        "\"snapshot_id\":\"" +
        std::string(kSnapshot) +
        "\","
        "\"generated_at\":\"2026-08-31T12:00:00+08:00\","
        "\"apps\":[{"
        "\"app_id\":\"cc.lilygo.ui.Demo\","
        "\"package\":\"lilygo-ui-demo\","
        "\"title\":\"演示\","
        "\"summary\":\"Registry 测试应用\","
        "\"categories\":[\"Utilities\"],"
        "\"icon_url\":\"" +
        icon_url +
        "\","
        "\"latest_version\":\"" +
        latest_version +
        "\","
        "\"detail_url\":\"" +
        detail_url + "\"}]}";
    const std::string index_url = std::string(kBase) + "/v1/snapshots/" + kSnapshot + "/index.json";
    const std::string root =
        "{"
        "\"protocol_version\":1,"
        "\"snapshot_id\":\"" +
        std::string(kSnapshot) +
        "\","
        "\"generated_at\":\"2026-08-31T12:00:00+08:00\","
        "\"index\":{\"url\":\"" +
        index_url + "\",\"sha256\":\"" + sha256(index) + "\"}}";
    Fixture result{std::string(kBase) + "/v1/root.json", {}};
    result.documents[result.root_url] = root;
    result.documents[index_url]       = index;
    result.documents[detail_url]      = detail;
    return result;
}

class FakeHttpClient final : public HttpClient {
public:
    std::map<std::string, std::string> documents;
    std::map<std::string, std::string> download_errors;
    std::vector<std::string> download_urls;
    std::string package_content = "payload";
    bool downloaded             = false;

    HttpTextResult get(const std::string &url, std::size_t maximum_size, bool) override
    {
        const auto found = documents.find(url);
        if (found == documents.end()) return {{}, "not found"};
        if (found->second.size() > maximum_size) return {{}, "too large"};
        return {found->second, {}};
    }

    HttpDownloadResult download(const std::string &url, const std::filesystem::path &destination,
                                std::uint64_t maximum_size, bool) override
    {
        download_urls.push_back(url);
        downloaded        = true;
        const auto failed = download_errors.find(url);
        if (failed != download_errors.end()) return {failed->second};
        if (package_content.size() > maximum_size) return {"too large"};
        std::ofstream output(destination, std::ios::binary);
        output << package_content;
        return output ? HttpDownloadResult{} : HttpDownloadResult{"write failed"};
    }
};

class FakeProcessRunner final : public ProcessRunner {
public:
    std::map<std::string, std::string> installed;
    std::map<std::string, std::string> installed_files;
    int inventory_exit_code = 0;
    std::string inventory_override;
    std::vector<std::vector<std::string>> calls;
    std::string install_package = "lilygo-ui-demo";
    std::string install_version = "1.2.3";
    std::string launcher_handoff_content;
    std::string package_install_error;
    std::string installed_status = "ii ";
    ProcessResult architecture_result{0, "arm64\n", {}, false};

    ProcessResult run(const std::vector<std::string> &arguments, std::size_t) override
    {
        calls.push_back(arguments);
        if (arguments.size() >= 2 && arguments[0] == "dpkg" && arguments[1] == "--print-architecture")
            return architecture_result;
        if (!arguments.empty() && arguments[0] == "dpkg-query") {
            if (arguments.size() == 5 && arguments[3] == "lilygo-*") {
                std::string output;
                for (const auto &entry : installed)
                    output += entry.first + "\t" + installed_status + "\t" + entry.second + "\tInstalled component\n";
                return {inventory_exit_code, inventory_override.empty() ? output : inventory_override, {}, false};
            }
            if (arguments.size() == 3 && arguments[1] == "--listfiles") {
                const auto files = installed_files.find(arguments.back());
                if (files != installed_files.end()) return {0, files->second, {}, false};
                const auto &package = arguments.back();
                if (package == "lilygo-ui-appkit-dev" || package == "lilygo-ui-mesa-vc4" || package == "meshtasticd")
                    return {0, "/usr/share/doc/" + package + "/README\n", {}, false};
                return {0, "/usr/share/launcher/apps/" + package + ".json\n", {}, false};
            }
            const auto found = installed.find(arguments.back());
            if (found == installed.end()) return {1, {}, "not installed", false};
            return {0, installed_status + "\t" + found->second, {}, false};
        }
        if (!arguments.empty() && arguments[0] == "pkexec") {
            if (arguments.size() == 6 && arguments[1] == LILYGO_UI_STORE_PACKAGE_INSTALL_HELPER) {
                if (!package_install_error.empty()) return {1, {}, package_install_error, false};
                if (arguments[2] == "lilygo-ui-launcher") {
                    launcher_handoff_content = read_binary(arguments[5]);
                    return {0, "queued lilygo-ui-launcher " + arguments[3] + "\n", {}, false};
                }
                installed[install_package] = install_version;
                return {0, {}, {}, false};
            }
            if (arguments.size() > 2 && arguments[2] == "--remove") {
                installed.erase(arguments.back());
                return {0, {}, {}, false};
            }
        }
        return {1, {}, "unexpected command", false};
    }
};

StoreApp make_store_app(std::string id, bool installed = false, bool update = false)
{
    StoreApp app;
    app.id               = std::move(id);
    app.app_id           = "cc.lilygo.ui.Demo";
    app.package          = "lilygo-ui-" + app.id;
    app.name             = "演示";
    app.summary          = "测试";
    app.version          = installed ? "1.0.0" : "1.2.3";
    app.latest_version   = "1.2.3";
    app.size             = "1.0 MB";
    app.installed        = installed;
    app.update_available = update;
    app.release.version  = "1.2.3";
    app.release.status   = "published";
    return app;
}

void test_router()
{
    AppRouter router;
    assert(router.page() == StorePage::catalog);
    router.show_installed();
    router.show_detail();
    assert(router.page() == StorePage::detail);
    assert(router.detail_origin() == StorePage::installed);
    assert(router.back());
    assert(router.page() == StorePage::installed);
    router.show_runtimes();
    router.show_detail();
    assert(router.detail_origin() == StorePage::runtimes);
    assert(router.back() && router.page() == StorePage::runtimes);
    assert(!router.back());
}

void test_model()
{
    auto first     = make_store_app("demo");
    first.official = true;
    auto second    = make_store_app("notes", true, true);
    second.app_id  = "cc.lilygo.ui.Notes";
    StoreModel model({first, second});

    assert(model.apps().size() == 2);
    model.set_filter(CatalogFilter::official);
    assert(model.matches_filter(model.apps()[0]));
    assert(!model.matches_filter(model.apps()[1]));
    model.set_filter(CatalogFilter::community);
    assert(!model.matches_filter(model.apps()[0]));
    assert(model.matches_filter(model.apps()[1]));
    model.set_filter(CatalogFilter::all);
    assert(model.matches_filter(model.apps()[0]));
    assert(model.matches_filter(model.apps()[1]));
    assert(model.install(0));
    assert(model.installed_count() == 2);
    assert(model.update_count() == 1);
    assert(model.update(1));
    assert(model.apps()[1].version == model.apps()[1].latest_version);
    assert(model.select(0));
    assert(model.selected_app() && model.selected_app()->id == "demo");
    assert(model.remove(model.selected_index()));
    assert(!model.apps()[0].installed);
}

void test_runtime_model_install_and_rollback_results()
{
    auto runtime    = make_store_app("runtime-history");
    runtime.runtime = true;
    StoreModel model({runtime});
    assert(model.install(0, "1.0.0"));
    assert(model.app(0)->installed && model.app(0)->version == "1.0.0");
    assert(model.app(0)->update_available);  // First install may deliberately choose an older published version.
    assert(!model.install(0, "1.2.3"));
    assert(model.update(0, "1.2.3"));
    assert(!model.app(0)->update_available);
    assert(model.update(0, "1.0.0"));  // A current runtime can be rolled back without an update badge.
    assert(model.app(0)->version == "1.0.0" && model.app(0)->update_available);
    assert(model.update(0, "1.3.0"));
    assert(!model.app(0)->update_available);
    assert(!model.remove(0));

    auto application = make_store_app("application", true, false);
    StoreModel ordinary({application});
    assert(!ordinary.update(0, "0.9.0"));
    assert(ordinary.app(0)->version == application.version);

    runtime.latest_version.clear();
    runtime.available = false;
    StoreModel uncatalogued({runtime});
    assert(uncatalogued.install(0, "1.0.0"));
    assert(!uncatalogued.app(0)->update_available);
}

void test_json_sha_and_debian_versions()
{
    const auto parsed = parse_json("{\"title\":\"\\u5929\\u6c14\",\"value\":1}");
    assert(parsed);
    const auto *object = parsed.value.object();
    assert(object && object->at("title").string());
    assert(*object->at("title").string() == "天气");
    assert(sha256("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    assert(debian_version_compare("1.0~rc1", "1.0") < 0);
    assert(debian_version_compare("1:1.0", "9.0") > 0);
    assert(debian_version_compare("1.0-2", "1.0-1") > 0);
    assert(debian_version_compare("1.01", "1.1") == 0);
}

void test_registry_snapshot_protocol()
{
    auto fixture = make_registry_fixture();
    FakeHttpClient http;
    http.documents = fixture.documents;
    RegistryClient client(http);
    const auto loaded = client.load(fixture.root_url);
    assert(loaded);
    assert(loaded.catalog.snapshot_id == kSnapshot);
    assert(loaded.catalog.apps.size() == 1);
    const auto &app = loaded.catalog.apps.front();
    assert(app.app_id == "cc.lilygo.ui.Demo");
    assert(app.latest_version == "1.2.3");
    assert(app.permissions.front().id == "network");
    assert(app.screenshots.size() == 1);
    assert(app.screenshots.front().caption == "主界面");

    const auto cached_root = fixture.documents.at(fixture.root_url);
    http.documents.erase(fixture.root_url);
    const auto loaded_from_cached_root = client.load_document(fixture.root_url, cached_root);
    assert(loaded_from_cached_root);
    assert(loaded_from_cached_root.catalog.snapshot_id == kSnapshot);
    assert(loaded_from_cached_root.catalog.apps.size() == 1);
    assert(loaded_from_cached_root.root_document == cached_root);

    fixture                     = make_registry_fixture();
    const auto lower_index_url  = std::string(kBase) + "/v1/snapshots/" + kSnapshot + "/index.json";
    const auto upper_index_url  = std::string("https://REGISTRY.TEST/v1/snapshots/") + kSnapshot + "/index.json";
    const auto lower_detail_url = std::string(kBase) + "/v1/snapshots/" + kSnapshot + "/apps/cc.lilygo.ui.Demo.json";
    const auto upper_detail_url =
        std::string("https://REGISTRY.TEST/v1/snapshots/") + kSnapshot + "/apps/cc.lilygo.ui.Demo.json";
    auto index_with_uppercase_host = fixture.documents.at(lower_index_url);
    const auto detail_url_position = index_with_uppercase_host.find(lower_detail_url);
    assert(detail_url_position != std::string::npos);
    index_with_uppercase_host.replace(detail_url_position, lower_detail_url.size(), upper_detail_url);
    auto root_with_uppercase_host = fixture.documents.at(fixture.root_url);
    const auto index_url_position = root_with_uppercase_host.find(lower_index_url);
    assert(index_url_position != std::string::npos);
    root_with_uppercase_host.replace(index_url_position, lower_index_url.size(), upper_index_url);
    const auto uppercase_hash_position = root_with_uppercase_host.find("\"sha256\":\"");
    assert(uppercase_hash_position != std::string::npos);
    root_with_uppercase_host.replace(uppercase_hash_position + 10, 64, sha256(index_with_uppercase_host));
    const auto detail_document = fixture.documents.at(lower_detail_url);
    fixture.documents.erase(lower_index_url);
    fixture.documents.erase(lower_detail_url);
    fixture.documents[fixture.root_url] = std::move(root_with_uppercase_host);
    fixture.documents[upper_index_url]  = std::move(index_with_uppercase_host);
    fixture.documents[upper_detail_url] = detail_document;
    http.documents                      = fixture.documents;
    const auto host_case_loaded         = client.load(fixture.root_url);
    assert(host_case_loaded);

    fixture                           = make_registry_fixture();
    http.documents                    = fixture.documents;
    auto &wrong_path_root             = http.documents[fixture.root_url];
    const auto expected_path_position = wrong_path_root.find(lower_index_url);
    assert(expected_path_position != std::string::npos);
    wrong_path_root.replace(expected_path_position, lower_index_url.size(),
                            std::string(kBase) + "/v1/other/index.json");
    const auto wrong_path = client.load(fixture.root_url);
    assert(!wrong_path);
    assert(wrong_path.error.find("snapshot_id") != std::string::npos);

    fixture              = make_registry_fixture();
    http.documents       = fixture.documents;
    const auto index_url = std::string(kBase) + "/v1/snapshots/" + kSnapshot + "/index.json";
    http.documents[index_url].push_back('\n');
    const auto rejected = client.load(fixture.root_url);
    assert(!rejected);
    assert(rejected.error.find("SHA-256") != std::string::npos);

    fixture                       = make_registry_fixture();
    http.documents                = fixture.documents;
    const auto old_timestamp      = std::string("2026-08-31T12:00:00+08:00");
    const auto new_timestamp      = std::string("2026-08-31T12:00:01+08:00");
    auto &changed_index           = http.documents[index_url];
    const auto timestamp_position = changed_index.find(old_timestamp);
    assert(timestamp_position != std::string::npos);
    changed_index.replace(timestamp_position, old_timestamp.size(), new_timestamp);
    auto &changed_root       = http.documents[fixture.root_url];
    const auto hash_marker   = std::string("\"sha256\":\"");
    const auto hash_position = changed_root.find(hash_marker);
    assert(hash_position != std::string::npos);
    changed_root.replace(hash_position + hash_marker.size(), 64, sha256(changed_index));
    const auto inconsistent = client.load(fixture.root_url);
    assert(!inconsistent);
    assert(inconsistent.error.find("generated_at") != std::string::npos);

    fixture                   = make_registry_fixture();
    http.documents            = fixture.documents;
    const auto detail_url     = std::string(kBase) + "/v1/snapshots/" + kSnapshot + "/apps/cc.lilygo.ui.Demo.json";
    auto &changed_detail      = http.documents[detail_url];
    const auto title_position = changed_detail.find("\"title\":\"演示\"");
    assert(title_position != std::string::npos);
    changed_detail.replace(title_position, std::string("\"title\":\"演示\"").size(), "\"title\":\"冒名应用\"");
    const auto mismatched_detail = client.load(fixture.root_url);
    assert(!mismatched_detail);
    assert(mismatched_detail.error.find("does not match") != std::string::npos);
}

void test_store_service_and_package_verification()
{
    auto fixture = make_registry_fixture();
    FakeHttpClient http;
    http.documents = fixture.documents;
    FakeProcessRunner runner;
    runner.installed["lilygo-ui-demo"]       = "1.0.0";
    runner.installed["lilygo-ui-appkit-dev"] = "0.1.0";
    RegistryClient registry(http);
    PackageManager packages(runner, http);
    StoreService service(registry, packages);
    const auto refreshed = service.refresh(fixture.root_url);
    assert(refreshed);
    assert(refreshed.apps.size() == 2);
    assert(refreshed.apps.front().installed);
    assert(refreshed.apps.front().update_available);
    assert(refreshed.apps.front().release.version == "1.2.3");
    assert(refreshed.apps.front().official);
    assert(refreshed.apps.front().icon_cache_key.find(kSnapshot) != std::string::npos);
    assert(refreshed.apps.front().icon_cache_key.find(refreshed.apps.front().icon_url) != std::string::npos);
    assert(refreshed.apps.front().screenshots.size() == 1);
    assert(refreshed.apps.front().screenshots.front().url == "https://registry.test/assets/screen.png");
    assert(refreshed.apps.front().screenshots.front().caption == "主界面");
    assert(refreshed.apps.front().screenshots.front().cache_key.find(kSnapshot) != std::string::npos);
    assert(refreshed.apps.front().screenshots.front().cache_key.find(refreshed.apps.front().screenshots.front().url) !=
           std::string::npos);
    assert(refreshed.apps.front().icon_cache_key != refreshed.apps.front().screenshots.front().cache_key);

    auto community_fixture = make_registry_fixture("https://github.com/LILYGO-UI-Community/demo");
    FakeHttpClient community_http;
    community_http.documents = community_fixture.documents;
    FakeProcessRunner community_runner;
    community_runner.installed["lilygo-ui-appkit-dev"] = "0.1.0";
    RegistryClient community_registry(community_http);
    PackageManager community_packages(community_runner, community_http);
    StoreService community_service(community_registry, community_packages);
    const auto community_refreshed = community_service.refresh(community_fixture.root_url);
    assert(community_refreshed);
    assert(community_refreshed.apps.size() == 2);
    assert(!community_refreshed.apps.front().official);

    auto traversal_fixture         = make_registry_fixture("https://github.com/LILYGO-UI/../attacker");
    community_http.documents       = traversal_fixture.documents;
    const auto traversal_refreshed = community_service.refresh(traversal_fixture.root_url);
    assert(traversal_refreshed);
    assert(traversal_refreshed.apps.size() == 2);
    assert(!traversal_refreshed.apps.front().official);

    const auto installed = packages.install("lilygo-ui-demo", refreshed.apps.front().release);
    assert(installed);
    assert(installed.installed_version == "1.2.3");
    assert(http.downloaded);

    runner.installed_status = "hi ";
    assert(packages.query("lilygo-ui-demo").installed);
    runner.installed_status = "ii ";

    runner.package_install_error  = "APT package installation failed: unmet dependencies";
    const auto dependency_failure = packages.install("lilygo-ui-demo", refreshed.apps.front().release);
    assert(!dependency_failure);
    assert(dependency_failure.error == runner.package_install_error);
    assert(!dependency_failure.launcher_update_queued);
    assert(!std::filesystem::exists(runner.calls.back().back()));
    runner.package_install_error.clear();

    auto bad_release                   = refreshed.apps.front().release;
    bad_release.sha256                 = std::string(64, '0');
    runner.installed["lilygo-ui-demo"] = "1.0.0";
    const auto read_only_package_query = [](const auto &arguments) {
        return arguments == std::vector<std::string>{"dpkg", "--print-architecture"} ||
               arguments == std::vector<std::string>{"dpkg-query", "--show",
                                                     "--showformat=${db:Status-Abbrev}\t${Version}", "lilygo-ui-demo"};
    };
    const auto call_count = runner.calls.size();
    const auto rejected   = packages.install("lilygo-ui-demo", bad_release);
    assert(!rejected);
    assert(rejected.error.find("SHA-256") != std::string::npos);
    assert(std::all_of(runner.calls.cbegin() + call_count, runner.calls.cend(), read_only_package_query));
    assert(runner.installed.at("lilygo-ui-demo") == "1.0.0");

    auto wrong_size = refreshed.apps.front().release;
    wrong_size.size += 1;
    const auto size_call_count = runner.calls.size();
    const auto size_rejected   = packages.install("lilygo-ui-demo", wrong_size);
    assert(!size_rejected);
    assert(size_rejected.error.find("size") != std::string::npos);
    assert(std::all_of(runner.calls.cbegin() + size_call_count, runner.calls.cend(), read_only_package_query));
    assert(runner.installed.at("lilygo-ui-demo") == "1.0.0");

    auto yanked   = refreshed.apps.front().release;
    yanked.status = "yanked";
    assert(!packages.install("lilygo-ui-demo", yanked));
    assert(!packages.remove(LILYGO_UI_STORE_PACKAGE_NAME));
    assert(!packages.remove("lilygo-ui-launcher"));

    auto launcher_release                  = refreshed.apps.front().release;
    runner.installed["lilygo-ui-launcher"] = "1.0.0";
    const auto launcher_update             = packages.install("lilygo-ui-launcher", launcher_release);
    assert(launcher_update);
    assert(launcher_update.launcher_update_queued);
    assert(launcher_update.installed_version == launcher_release.version);
    assert(runner.installed["lilygo-ui-launcher"] == "1.0.0");
    assert(runner.launcher_handoff_content == http.package_content);
    const auto &handoff = runner.calls.back();
    assert(handoff.size() == 6);
    assert(handoff[0] == "pkexec");
    assert(handoff[1] == LILYGO_UI_STORE_PACKAGE_INSTALL_HELPER);
    assert(handoff[2] == "lilygo-ui-launcher");
    assert(handoff[3] == launcher_release.version);
    assert(handoff[4] == launcher_release.sha256);

    runner.installed.erase("lilygo-ui-launcher");
    const auto download_count   = http.download_urls.size();
    const auto missing_baseline = packages.install("lilygo-ui-launcher", launcher_release);
    assert(!missing_baseline);
    assert(missing_baseline.error.find("must be installed first") != std::string::npos);
    assert(http.download_urls.size() == download_count);

    const auto removed = packages.remove("lilygo-ui-demo");
    assert(removed);
    assert(!packages.query("lilygo-ui-demo").installed);
}

void rehash_fixture(Fixture &fixture)
{
    const auto index_url = std::string(kBase) + "/v1/snapshots/" + kSnapshot + "/index.json";
    auto &root           = fixture.documents[fixture.root_url];
    const auto marker    = root.find("\"sha256\":\"");
    assert(marker != std::string::npos);
    root.replace(marker + 10, 64, sha256(fixture.documents[index_url]));
}

Fixture make_runtime_fixture(const std::vector<RegistryRelease> &releases)
{
    auto fixture           = make_registry_fixture("https://github.com/LILYGO-UI/demo", releases);
    const auto base        = std::string(kBase) + "/v1/snapshots/" + kSnapshot;
    const auto &app_detail = fixture.documents[base + "/apps/cc.lilygo.ui.Demo.json"];
    auto release_json      = app_detail.substr(app_detail.find("\"releases\":"));
    // A runtime has no minimum AppKit requirement, desktop ID, icon or screenshots.
    for (const auto &release : releases) {
        const auto field = ",\"min_appkit_version\":\"" + release.min_appkit_version + "\"";
        const auto found = release_json.find(field);
        assert(found != std::string::npos);
        release_json.erase(found, field.size());
    }
    const auto detail = std::string(R"({"protocol_version":1,"package":"lilygo-ui-appkit-dev",
        "title":"AppKit","summary":"Shared fonts","description":"Shared fonts and SDK",
        "authors":[{"name":"LILYGO","github":"LILYGO-UI"}],)") +
                        release_json;
    const auto url         = base + "/runtimes/lilygo-ui-appkit-dev.json";
    fixture.documents[url] = detail;
    auto &index            = fixture.documents[base + "/index.json"];
    // Reuse the global latest version from the application fixture.
    const auto version_start = index.find("\"latest_version\":\"") + 18;
    const auto latest        = index.substr(version_start, index.find('"', version_start) - version_start);
    index.pop_back();
    index += std::string(R"(,"runtimes":[{"package":"lilygo-ui-appkit-dev","title":"AppKit",
        "summary":"Shared fonts","latest_version":")") +
             latest + "\",\"detail_url\":\"" + url + "\",\"detail_sha256\":\"" + sha256(detail) + "\"}]}";
    rehash_fixture(fixture);
    return fixture;
}

void test_runtime_catalog_and_updates()
{
    const auto fixture = make_runtime_fixture(
        {make_registry_release("0.2.0", "all"), make_registry_release("0.3.0", "arm64"),
         make_registry_release("0.4.0", "riscv64"), make_registry_release("0.9.0", "arm64", "yanked"),
         make_registry_release("0.8.0", "all", "revoked")});
    FakeHttpClient http;
    http.documents = fixture.documents;
    RegistryClient registry(http);
    const auto loaded = registry.load(fixture.root_url);
    assert(loaded && loaded.catalog.apps.size() == 2);
    assert(loaded.catalog.apps.back().runtime);
    assert(loaded.catalog.apps.back().app_id == "runtime:lilygo-ui-appkit-dev");
    assert(loaded.catalog.apps.back().releases.front().min_appkit_version.empty());
    FakeProcessRunner runner;
    runner.installed["lilygo-ui-appkit-dev"] = "0.1.0";
    PackageManager packages(runner, http);
    StoreService service(registry, packages);
    for (const auto &architecture : {"arm64", "riscv64", "amd64"}) {
        runner.architecture_result.output = std::string(architecture) + "\n";
        const auto refreshed              = service.refresh(fixture.root_url);
        assert(refreshed && refreshed.apps.size() == 2);
        const auto &runtime = refreshed.apps.back();
        assert(runtime.runtime && runtime.installed && runtime.update_available);
        assert(runtime.version == "0.1.0");
        const auto expected = std::string(architecture) == "arm64"
                                  ? "0.3.0"
                                  : (std::string(architecture) == "riscv64" ? "0.4.0" : "0.2.0");
        assert(runtime.latest_version == expected);
    }
    runner.architecture_result.output        = "arm64\n";
    runner.installed["lilygo-ui-appkit-dev"] = "0.5.0";
    assert(!service.refresh(fixture.root_url).apps.back().update_available);
    runner.installed["lilygo-ui-appkit-dev"] = "0.3.0";
    assert(!service.refresh(fixture.root_url).apps.back().update_available);
    runner.installed["lilygo-ui-appkit-dev"] = "0.1.0";
    runner.install_package                   = "lilygo-ui-appkit-dev";
    runner.install_version                   = "0.3.0";
    const auto runtime                       = service.refresh(fixture.root_url).apps.back();
    const auto updated                       = packages.install(runtime.package, runtime.release);
    assert(updated && updated.installed_version == "0.3.0");
    assert(runner.installed[runtime.package] == "0.3.0");

    const auto detail_url = std::string(kBase) + "/v1/snapshots/" + kSnapshot + "/runtimes/lilygo-ui-appkit-dev.json";
    http.documents[detail_url] += " ";
    assert(registry.load(fixture.root_url).error.find("SHA-256 mismatch") != std::string::npos);
    auto invalid = fixture;
    auto &index  = invalid.documents[std::string(kBase) + "/v1/snapshots/" + kSnapshot + "/index.json"];
    index.replace(index.find(detail_url), detail_url.size(), "https://elsewhere.test/runtime.json");
    rehash_fixture(invalid);
    http.documents = invalid.documents;
    assert(registry.load(fixture.root_url).error.find("outside the current snapshot") != std::string::npos);

    auto foreign           = make_runtime_fixture({make_registry_release("0.4.0", "riscv64")});
    http.documents         = foreign.documents;
    const auto unavailable = service.refresh(foreign.root_url);
    assert(unavailable && unavailable.apps.size() == 1);
    assert(unavailable.apps[0].runtime && unavailable.apps[0].installed && !unavailable.apps[0].available);
    assert(!unavailable.apps[0].update_available);

    // Runtime documents participate in the same complete offline snapshot cache.
    const auto cache_path =
        std::filesystem::temp_directory_path() / ("store-runtime-cache-" + std::to_string(getpid()));
    ScopedEnvironmentVariable cache_home("XDG_CACHE_HOME", cache_path.c_str());
    RegistryRootCache cache;
    std::vector<RegistryCacheDocument> documents;
    for (const auto &entry : fixture.documents) documents.push_back({entry.first, entry.second});
    assert(cache.store(fixture.root_url, documents).empty());
    FakeHttpClient cached_http;
    for (const auto &entry : fixture.documents) {
        const auto cached = cache.load_document(fixture.root_url, entry.first, 4 * 1024 * 1024);
        assert(cached);
        cached_http.documents[entry.first] = cached.document;
    }
    assert(RegistryClient(cached_http).load(fixture.root_url).catalog.apps.back().runtime);
    std::filesystem::remove_all(cache_path);
}

void test_runtime_release_history_and_complete_catalog()
{
    std::vector<RegistryRelease> releases{
        make_registry_release("1.9.0", "all"),
        make_registry_release("1.10.0", "all"),
        make_registry_release("1.10.0", "arm64"),
        make_registry_release("1:1.0-1", "arm64", "published", "99.0"),
        make_registry_release("2:1.0-1", "riscv64"),
        make_registry_release("9:9.0", "arm64", "yanked"),
        make_registry_release("8:8.0", "all", "revoked"),
        make_registry_release("1.1", "all"),
        make_registry_release("1.01", "arm64"),
    };
    ScopedEnvironmentVariable appkit_version("LILYGO_UI_APPKIT_VERSION", "0.1.0");
    for (int order = 0; order < 2; ++order) {
        const auto fixture = make_runtime_fixture(releases);
        FakeHttpClient http;
        http.documents = fixture.documents;
        FakeProcessRunner runner;
        RegistryClient registry(http);
        PackageManager packages(runner, http);
        StoreService service(registry, packages);
        const auto result = service.refresh(fixture.root_url);
        assert(result);
        const auto runtime =
            std::find_if(result.apps.begin(), result.apps.end(), [](const auto &app) { return app.runtime; });
        assert(runtime != result.apps.end());
        assert(!runtime->installed && runtime->available && runtime->catalogued);
        assert(runtime->latest_version == "1:1.0-1");
        assert(runtime->release.version == runtime->runtime_releases.front().version);
        assert(runtime->runtime_releases.size() == 4);
        const std::vector<std::string> expected{"1:1.0-1", "1.10.0", "1.9.0", "1.01"};
        for (std::size_t index = 0; index < expected.size(); ++index) {
            const auto &release = runtime->runtime_releases[index];
            assert(release.version == expected[index] && release.status == "published");
            assert(release.architecture == "arm64" || release.architecture == "all");
        }
        assert(runtime->runtime_releases[1].architecture == "arm64");
        assert(runtime->runtime_releases[3].architecture == "arm64");

        runner.installed["lilygo-ui-appkit-dev"] = "1.0.0";
        const auto installed                     = service.refresh(fixture.root_url);
        assert(installed);
        assert(std::count_if(installed.apps.begin(), installed.apps.end(),
                             [](const auto &app) { return app.package == "lilygo-ui-appkit-dev"; }) == 1);
        const auto catalogued =
            std::find_if(installed.apps.begin(), installed.apps.end(), [](const auto &app) { return app.runtime; });
        assert(catalogued->installed && catalogued->catalogued && catalogued->name == "AppKit");
        assert(catalogued->runtime_releases.size() == 4);
        std::reverse(releases.begin(), releases.end());
    }

    const auto fixture = make_runtime_fixture({make_registry_release("4.0.0", "riscv64")});
    FakeHttpClient http;
    http.documents = fixture.documents;
    FakeProcessRunner runner;
    RegistryClient registry(http);
    PackageManager packages(runner, http);
    const auto incompatible = StoreService(registry, packages).refresh(fixture.root_url);
    assert(incompatible && incompatible.apps.size() == 1);
    const auto &runtime = incompatible.apps.front();
    assert(runtime.runtime && runtime.catalogued && !runtime.installed && !runtime.available);
    assert(runtime.runtime_releases.empty() && runtime.release.url.empty());
}

void test_local_runtimes_without_registry_metadata()
{
    const auto fixture = make_registry_fixture();  // Legacy app-only index, as deployed on Pages.
    FakeHttpClient http;
    http.documents = fixture.documents;
    FakeProcessRunner runner;
    runner.installed["lilygo-ui-appkit-dev"] = "0.1.1";
    runner.installed["lilygo-ui-mesa-vc4"]   = "26.2.0-2";
    runner.installed["meshtasticd"]          = "2.8.0.0+lilygo1";
    RegistryClient registry(http);
    PackageManager packages(runner, http);
    StoreService service(registry, packages);
    auto result = service.refresh(fixture.root_url);
    assert(result && result.apps.size() == 4);
    for (std::size_t index = 1; index < result.apps.size(); ++index) {
        const auto &app = result.apps[index];
        assert(app.runtime && app.installed && !app.catalogued && !app.available && !app.update_available);
        assert(app.version == runner.installed[app.package]);
        assert(app.latest_version.empty() && app.release.url.empty() && app.runtime_releases.empty());
    }
    assert(result.apps.back().package == "meshtasticd");
    http.documents.clear();  // First launch offline, without any Registry cache.
    result = service.refresh(fixture.root_url);
    assert(!result && result.apps.size() == 3);
    assert(result.apps.back().installed && result.apps.back().package == "meshtasticd");
    runner.installed.erase("lilygo-ui-mesa-vc4");
    result = service.refresh(fixture.root_url);
    assert(!result && result.apps.size() == 2);
    runner.installed_status = "rc ";
    assert(service.refresh(fixture.root_url).apps.empty());

    runner.installed_status = "ii ";
    runner.install_package  = "meshtasticd";
    runner.install_version  = "2.9.0";
    assert(packages.query("meshtasticd").installed);
    const auto updated = packages.install("meshtasticd", make_registry_release("2.9.0"));
    assert(updated && updated.installed_version == "2.9.0");
    for (const auto *invalid : {"--help", "foo;bar", "foo/bar", "foo bar", "foo*", "foo:arm64", "Foo"}) {
        const auto count = runner.calls.size();
        assert(!packages.query(invalid).error.empty());
        assert(!packages.install(invalid, make_registry_release()));
        assert(runner.calls.size() == count);
    }
}

void test_runtime_inventory_detects_components_without_launchers()
{
    FakeHttpClient http;
    FakeProcessRunner runner;
    runner.installed = {{"lilygo-t-display-cm0-overlay", "1.0.0-1"},
                        {"lilygo-future-runtime", "2.0"},
                        {"meshtasticd", "2.8.0"},
                        {"lilygo-ui-launcher", "1.0"},
                        {"lilygo-ui-unlisted-app", "1.0"},
                        {"lilygo-desktop-tool", "1.0"},
                        {"libc6", "2.41"}};
    runner.installed_files["lilygo-t-display-cm0-overlay"] =
        "/usr/share/lilygo-t-display-cm0-overlay/release.json\n/usr/share/lilygo-t-display-cm0-overlay/"
        "t-display-cm0.dtbo\n";
    runner.installed_files["lilygo-future-runtime"] = "/usr/lib/lilygo-future-runtime/libfuture.so\n";
    runner.installed_files["lilygo-desktop-tool"]   = "/usr/share/applications/tool.desktop\n";
    PackageManager packages(runner, http);
    const auto runtimes = packages.installed_runtimes();
    assert(runtimes.size() == 3);
    assert(std::any_of(runtimes.begin(), runtimes.end(), [](const auto &item) {
        return item.package == "lilygo-t-display-cm0-overlay" && item.version == "1.0.0-1";
    }));
    RegistryClient registry(http);
    auto offline = StoreService(registry, packages).refresh(kDefaultRegistryRoot);
    assert(!offline && offline.apps.size() == 3);
    for (const auto &item : offline.apps) assert(item.runtime && item.installed && !item.catalogued && !item.available);
    runner.installed.erase("meshtasticd");
    runner.inventory_exit_code = 1;  // An unmatched external package must not hide LILYGO components.
    assert(packages.installed_runtimes().size() == 2);
    runner.installed_status = "hi ";
    assert(packages.installed_runtimes().size() == 2);
    runner.installed_status = "rc ";
    assert(packages.installed_runtimes().empty());
    runner.installed_status = "iU ";
    assert(packages.installed_runtimes().empty());
    runner.installed_status    = "ii ";
    runner.inventory_exit_code = 2;
    assert(packages.installed_runtimes().empty());
    runner.inventory_exit_code = 0;
    runner.inventory_override =
        "lilygo-future-runtime\tii \t2.0\tFuture\n"
        "lilygo-future-runtime\tii \t2.0\tDuplicate\n"
        "--help\tii \t1.0\tInvalid\n"
        "libc6\tii \t2.41\tUnrelated system package\n";
    assert(packages.installed_runtimes().size() == 1);
}

void test_store_architecture_selection()
{
    const auto refresh = [](const std::vector<RegistryRelease> &releases, const std::string &architecture,
                            const std::string &installed_version = std::string{}) {
        const auto fixture = make_registry_fixture("https://github.com/LILYGO-UI/demo", releases);
        FakeHttpClient http;
        http.documents = fixture.documents;
        FakeProcessRunner runner;
        runner.architecture_result.output        = architecture + "\n";
        runner.installed["lilygo-ui-appkit-dev"] = "0.1.0";
        if (!installed_version.empty()) runner.installed["lilygo-ui-demo"] = installed_version;
        RegistryClient registry(http);
        PackageManager packages(runner, http);
        return StoreService(registry, packages).refresh(fixture.root_url);
    };

    for (const auto &architecture : {std::string("arm64"), std::string("riscv64")}) {
        const auto other_architecture = architecture == "arm64" ? "riscv64" : "arm64";
        std::vector<RegistryRelease> mixed{
            make_registry_release("1.9.0", architecture),
            make_registry_release("2:1.0-1", other_architecture),
            make_registry_release("1:1.0-2", architecture),
            make_registry_release("1.10.0", architecture),
        };
        for (int order = 0; order < 2; ++order) {
            const auto result = refresh(mixed, architecture, "1:1.0-1");
            assert(result && result.apps.size() == 2);
            const auto &app = result.apps.front();
            assert(app.available && app.installed && app.update_available);
            assert(app.version == "1:1.0-1");
            assert(app.latest_version == "1:1.0-2");
            assert(app.release.version == "1:1.0-2");
            assert(app.release.architecture == architecture);
            assert(app.release.url == make_registry_release("1:1.0-2", architecture).url);
            std::reverse(mixed.begin(), mixed.end());
        }

        const auto up_to_date = refresh(mixed, architecture, "1:1.0-2");
        assert(up_to_date && up_to_date.apps.size() == 2);
        assert(!up_to_date.apps.front().update_available);
        assert(up_to_date.apps.front().latest_version == "1:1.0-2");

        const auto universal = refresh({make_registry_release("1.2.3", "all")}, architecture);
        assert(universal && universal.apps.size() == 2);
        assert(universal.apps.front().available);
        assert(universal.apps.front().release.architecture == "all");

        const auto newer_universal = refresh(
            {make_registry_release("1.0.0", architecture), make_registry_release("2.0.0", "all")}, architecture);
        assert(newer_universal && newer_universal.apps.size() == 2);
        assert(newer_universal.apps.front().latest_version == "2.0.0");
        assert(newer_universal.apps.front().release.architecture == "all");

        std::vector<RegistryRelease> tied{make_registry_release("1.2.3", "all"),
                                          make_registry_release("1.2.3", architecture)};
        for (int order = 0; order < 2; ++order) {
            const auto preferred_native = refresh(tied, architecture);
            assert(preferred_native && preferred_native.apps.size() == 2);
            assert(preferred_native.apps.front().release.architecture == architecture);
            std::reverse(tied.begin(), tied.end());
        }

        const auto eligible =
            refresh({make_registry_release("9.0.0", architecture, "yanked"),
                     make_registry_release("8.0.0", architecture, "revoked"),
                     make_registry_release("7.0.0", architecture, "published", "0.2.0"),
                     make_registry_release("6.0.0", "all", "published", "0.2.0"),
                     make_registry_release("5.0.0", other_architecture), make_registry_release("1.2.3", architecture)},
                    architecture);
        assert(eligible && eligible.apps.size() == 2);
        assert(eligible.apps.front().release.architecture == architecture);
        assert(eligible.apps.front().latest_version == "1.2.3");

        const std::vector<RegistryRelease> foreign_only{make_registry_release("9.0.0", other_architecture)};
        const auto hidden = refresh(foreign_only, architecture);
        assert(hidden && hidden.apps.size() == 1 && hidden.apps[0].runtime);

        const auto retained = refresh(foreign_only, architecture, "1.0.0");
        assert(retained && retained.apps.size() == 2);
        const auto &installed = retained.apps.front();
        assert(installed.installed && !installed.available && !installed.update_available);
        assert(installed.version == "1.0.0" && installed.latest_version == "1.0.0");
        assert(installed.release.version.empty() && installed.release.url.empty());

        StoreModel model(retained.apps);
        assert(model.matches_filter(model.apps().front()));
        assert(model.remove(0));
        for (const auto filter : {CatalogFilter::all, CatalogFilter::official, CatalogFilter::community}) {
            model.set_filter(filter);
            assert(!model.matches_filter(model.apps().front()));
        }
    }
}

void test_package_architecture_detection_and_installation()
{
    FakeHttpClient http;
    FakeProcessRunner runner;
    PackageManager packages(runner, http);
    runner.architecture_result.output = "riscv64\n";
    assert(packages.architecture() == "riscv64");
    {
        ScopedEnvironmentVariable override_architecture("LILYGO_UI_STORE_ARCHITECTURE", "arm64");
        const auto call_count = runner.calls.size();
        assert(packages.architecture() == "arm64");
        assert(runner.calls.size() == call_count);
    }
    assert(packages.architecture() == "riscv64");
    runner.architecture_result = {1, {}, "dpkg unavailable", false};
#if defined(__aarch64__) || defined(_M_ARM64)
    assert(packages.architecture() == "arm64");
#elif defined(__x86_64__) || defined(_M_X64)
    assert(packages.architecture() == "amd64");
#elif defined(__riscv) && __riscv_xlen == 64
    assert(packages.architecture() == "riscv64");
#else
    assert(packages.architecture().empty());
#endif

    for (const auto &architecture : {std::string("arm64"), std::string("riscv64")}) {
        runner.architecture_result    = {0, architecture + "\n", {}, false};
        const auto other_architecture = architecture == "arm64" ? "riscv64" : "arm64";
        runner.calls.clear();
        http.download_urls.clear();
        const auto rejected = packages.install("lilygo-ui-demo", make_registry_release("1.2.3", other_architecture));
        assert(!rejected);
        assert(rejected.error.find("architecture") != std::string::npos);
        assert(http.download_urls.empty());
        assert(std::none_of(runner.calls.cbegin(), runner.calls.cend(),
                            [](const auto &arguments) { return !arguments.empty() && arguments.front() == "pkexec"; }));
        for (const auto &accepted_architecture : {architecture, std::string("all")}) {
            runner.calls.clear();
            http.download_urls.clear();
            const auto release   = make_registry_release("1.2.3", accepted_architecture);
            const auto installed = packages.install("lilygo-ui-demo", release);
            assert(installed && installed.installed_version == "1.2.3");
            assert(http.download_urls == std::vector<std::string>{release.url});
            assert(std::any_of(runner.calls.cbegin(), runner.calls.cend(), [](const auto &arguments) {
                return arguments.size() > 1 && arguments[0] == "pkexec" &&
                       arguments[1] == LILYGO_UI_STORE_PACKAGE_INSTALL_HELPER;
            }));
        }
    }
}

void test_github_api_package_download()
{
    constexpr auto digest         = "239f59ed55e737c77147cf55ad0c1b030b6d7ee748a7426952f9b852d5a935e5";
    const std::string package     = "lilygo-ui-demo";
    const std::string tag         = "apt-pool-" + package;
    const std::string asset_name  = std::string("sha256-") + digest + ".deb";
    const std::string release_url = "https://github.com/LILYGO-UI/packages/releases/download/" + tag + "/" + asset_name;
    const std::string metadata_url = "https://api.github.com/repos/LILYGO-UI/packages/releases/tags/" + tag;
    const std::string asset_url    = "https://api.github.com/repos/LILYGO-UI/packages/releases/assets/12345";

    RegistryRelease release{"1.2.3", "arm64", "published", 7, digest, release_url, "0.1.0", "2026-08-31T12:00:00+08:00",
                            {}};
    FakeHttpClient http;
    http.documents[metadata_url] = "{\"tag_name\":\"" + tag + "\",\"assets\":[{\"name\":\"" + asset_name +
                                   "\",\"size\":7,\"digest\":\"sha256:" + digest +
                                   "\",\"state\":\"uploaded\",\"url\":\"" + asset_url + "\"}]}";
    FakeProcessRunner runner;
    PackageManager packages(runner, http);
    const auto installed = packages.install(package, release);
    assert(installed);
    assert(http.download_urls.size() == 1);
    assert(http.download_urls.front() == asset_url);

    FakeHttpClient fallback_http;
    fallback_http.documents[metadata_url] = "{\"tag_name\":\"" + tag + "\",\"assets\":[{\"name\":\"" + asset_name +
                                            "\",\"size\":8,\"digest\":\"sha256:" + digest +
                                            "\",\"state\":\"uploaded\",\"url\":\"" + asset_url + "\"}]}";
    FakeProcessRunner fallback_runner;
    PackageManager fallback_packages(fallback_runner, fallback_http);
    const auto fallback = fallback_packages.install(package, release);
    assert(fallback);
    assert(fallback_http.download_urls.size() == 1);
    assert(fallback_http.download_urls.front() == release_url);

    FakeHttpClient origin_http;
    origin_http.documents[metadata_url] = "{\"tag_name\":\"" + tag + "\",\"assets\":[{\"name\":\"" + asset_name +
                                          "\",\"size\":7,\"digest\":\"sha256:" + digest +
                                          "\",\"state\":\"uploaded\",\"url\":"
                                          "\"https://attacker.example/assets/12345\"}]}";
    FakeProcessRunner origin_runner;
    PackageManager origin_packages(origin_runner, origin_http);
    assert(origin_packages.install(package, release));
    assert(origin_http.download_urls.size() == 1);
    assert(origin_http.download_urls.front() == release_url);

    FakeHttpClient failed_asset_http;
    failed_asset_http.documents                  = http.documents;
    failed_asset_http.download_errors[asset_url] = "API asset unavailable";
    FakeProcessRunner failed_asset_runner;
    PackageManager failed_asset_packages(failed_asset_runner, failed_asset_http);
    assert(failed_asset_packages.install(package, release));
    assert(failed_asset_http.download_urls.size() == 2);
    assert(failed_asset_http.download_urls.front() == asset_url);
    assert(failed_asset_http.download_urls.back() == release_url);
}

void test_registry_root_cache()
{
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto test_root =
        std::filesystem::temp_directory_path() /
        ("lilygo-ui-store-root-cache-test-" + std::to_string(getpid()) + "-" + std::to_string(nonce));
    assert(std::filesystem::create_directories(test_root));

    const auto *previous_cache_value = std::getenv("XDG_CACHE_HOME");
    const bool had_previous_cache    = previous_cache_value != nullptr;
    const std::string previous_cache = had_previous_cache ? previous_cache_value : std::string{};
    assert(setenv("XDG_CACHE_HOME", test_root.string().c_str(), 1) == 0);

    RegistryRootCache cache;
    const std::string root_url  = "https://registry.test/v1/root.json";
    const std::string other_url = "https://other.test/v1/root.json";
    const std::string index_url = "https://registry.test/v1/snapshots/one/index.json";
    assert(!cache.load(root_url));
    assert(cache.store(root_url, {{root_url, "{\"snapshot\":1}"}, {index_url, "{\"apps\":[1]}"}}).empty());
    const auto first = cache.load(root_url);
    assert(first && first.document == "{\"snapshot\":1}");
    const auto first_index = cache.load_document(root_url, index_url, 1024);
    assert(first_index && first_index.document == "{\"apps\":[1]}");
    assert(!cache.load(other_url));

    assert(cache.store(root_url, {{root_url, "{\"snapshot\":2}"}, {index_url, "{\"apps\":[2]}"}}).empty());
    const auto replaced = cache.load(root_url);
    assert(replaced && replaced.document == "{\"snapshot\":2}");
    const auto replaced_index = cache.load_document(root_url, index_url, 1024);
    assert(replaced_index && replaced_index.document == "{\"apps\":[2]}");
    assert(!cache.store(root_url, {{root_url, std::string(64U * 1024U + 1U, 'x')}}).empty());
    const auto preserved = cache.load(root_url);
    assert(preserved && preserved.document == "{\"snapshot\":2}");

    if (had_previous_cache)
        assert(setenv("XDG_CACHE_HOME", previous_cache.c_str(), 1) == 0);
    else
        assert(unsetenv("XDG_CACHE_HOME") == 0);
    std::error_code cleanup_error;
    std::filesystem::remove_all(test_root, cleanup_error);
    assert(!cleanup_error);
}

void test_image_download_validation_and_curl_options()
{
    const auto nonce     = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto test_root = std::filesystem::temp_directory_path() /
                           ("lilygo-ui-store-image-test-" + std::to_string(getpid()) + "-" + std::to_string(nonce));
    assert(!std::filesystem::exists(test_root));
    assert(std::filesystem::create_directories(test_root));

    const auto *previous_cache_value = std::getenv("XDG_CACHE_HOME");
    const bool had_previous_cache    = previous_cache_value != nullptr;
    const std::string previous_cache = had_previous_cache ? previous_cache_value : std::string{};
    const auto cache_home            = test_root / "cache";
    assert(setenv("XDG_CACHE_HOME", cache_home.string().c_str(), 1) == 0);

    const auto source = std::filesystem::path(__FILE__).parent_path().parent_path() / "assets" / "app-icon.png";
    assert(std::filesystem::is_regular_file(source));
    const auto source_url = file_url(source);
    const auto first      = download_store_image(source_url, std::string(kSnapshot) + ":" + source_url);
    assert(first);
    assert(std::filesystem::is_regular_file(first.path));

    const auto second = download_store_image(source_url, std::string("different-snapshot:") + source_url);
    assert(second);
    assert(std::filesystem::is_regular_file(second.path));
    assert(first.path != second.path);

    const auto preview_source =
        std::filesystem::path(__FILE__).parent_path().parent_path() / "assets" / "previews" / "store-portrait.png";
    assert(std::filesystem::is_regular_file(preview_source));
    const auto preview_png    = read_binary(preview_source);
    const auto preview_header = find_png_chunk(preview_png, "IHDR");
    assert(preview_header != std::string::npos);
    const auto preview_width  = read_png_u32(preview_png, preview_header + 8);
    const auto preview_height = read_png_u32(preview_png, preview_header + 12);
    assert(preview_width == 568);
    assert(preview_height == 1232);

    const auto preview_url = file_url(preview_source);
    const auto normalized  = download_store_image(preview_url, std::string("normalized-preview:") + preview_url);
    assert(normalized);
    const auto normalized_png    = read_binary(normalized.path);
    const auto normalized_header = find_png_chunk(normalized_png, "IHDR");
    assert(normalized_header != std::string::npos);
    const auto normalized_width  = read_png_u32(normalized_png, normalized_header + 8);
    const auto normalized_height = read_png_u32(normalized_png, normalized_header + 12);
    assert(static_cast<std::uint64_t>(normalized_width) * normalized_height <= 240000U);
    assert(normalized_width < preview_width);
    assert(normalized_height < preview_height);

    const auto valid_png = read_binary(source);
    assert(valid_png.size() > 12);
    assert(valid_png.compare(valid_png.size() - 8, 4, "IEND") == 0);

    auto missing_iend = valid_png;
    missing_iend.resize(missing_iend.size() - 12);
    const auto missing_iend_path = test_root / "inputs" / "missing-iend.png";
    write_binary(missing_iend_path, missing_iend);
    const auto missing_iend_url = file_url(missing_iend_path);
    const auto rejected_iend = download_store_image(missing_iend_url, std::string("missing-iend:") + missing_iend_url);
    assert(!rejected_iend);
    assert(rejected_iend.path.empty());

    auto bad_crc                 = valid_png;
    const auto image_data_marker = bad_crc.find("IDAT");
    assert(image_data_marker != std::string::npos);
    assert(image_data_marker + 4 < bad_crc.size());
    bad_crc[image_data_marker + 4] = static_cast<char>(bad_crc[image_data_marker + 4] ^ 0x01);
    const auto bad_crc_path        = test_root / "inputs" / "bad-crc.png";
    write_binary(bad_crc_path, bad_crc);
    const auto bad_crc_url  = file_url(bad_crc_path);
    const auto rejected_crc = download_store_image(bad_crc_url, std::string("bad-crc:") + bad_crc_url);
    assert(!rejected_crc);
    assert(rejected_crc.path.empty());

    auto invalid_image_data     = valid_png;
    const auto image_data_chunk = find_png_chunk(invalid_image_data, "IDAT");
    assert(image_data_chunk != std::string::npos);
    const auto image_data_size = static_cast<std::size_t>(read_png_u32(invalid_image_data, image_data_chunk));
    assert(image_data_size > 2);
    const auto image_data_offset = image_data_chunk + 8;
    assert(static_cast<unsigned char>(invalid_image_data[image_data_offset]) != 0);
    invalid_image_data[image_data_offset] = 0;
    const auto recomputed_crc             = png_crc(invalid_image_data, image_data_chunk + 4, image_data_size + 4);
    const auto crc_offset                 = image_data_offset + image_data_size;
    write_png_u32(invalid_image_data, crc_offset, recomputed_crc);
    assert(read_png_u32(invalid_image_data, crc_offset) ==
           png_crc(invalid_image_data, image_data_chunk + 4, image_data_size + 4));
    const auto invalid_image_data_path = test_root / "inputs" / "invalid-image-data.png";
    write_binary(invalid_image_data_path, invalid_image_data);
    const auto invalid_image_data_url = file_url(invalid_image_data_path);
    const auto rejected_decode =
        download_store_image(invalid_image_data_url, std::string("invalid-image-data:") + invalid_image_data_url);
    assert(!rejected_decode);
    assert(rejected_decode.path.empty());
    assert(rejected_decode.error.find("Unable to prepare image for display") != std::string::npos);

    FakeProcessRunner curl_runner;
    CurlHttpClient curl(curl_runner);
    const auto binary = curl.get_binary("https://registry.test/assets/icon.png", 4096);
    assert(!binary);
    assert(curl_runner.calls.size() == 1);
    const auto &arguments = curl_runner.calls.front();
    assert(!arguments.empty() && arguments.front() == "curl");
    assert(has_argument_pair(arguments, "--header", "Accept: image/png"));
    assert(has_argument_pair(arguments, "--max-time", "30"));

    const auto destination = test_root / "package.deb";
    const auto downloaded  = curl.download("https://registry.test/package.deb", destination, 4096);
    assert(!downloaded);
    assert(curl_runner.calls.size() == 2);
    assert(has_argument_pair(curl_runner.calls.back(), "--header", "Accept: application/octet-stream"));

    if (had_previous_cache)
        assert(setenv("XDG_CACHE_HOME", previous_cache.c_str(), 1) == 0);
    else
        assert(unsetenv("XDG_CACHE_HOME") == 0);
    std::error_code cleanup_error;
    std::filesystem::remove_all(test_root, cleanup_error);
    assert(!cleanup_error);
}

}  // namespace

int main(int argc, char **argv)
{
    if (argc == 3 && std::string(argv[1]) == "--registry-root") {
        PosixProcessRunner runner;
        CurlHttpClient http(runner);
        const auto result = RegistryClient(http).load(argv[2]);
        if (!result) {
            std::fprintf(stderr, "%s\n", result.error.c_str());
            return 1;
        }
        for (const auto &package : result.catalog.apps)
            std::printf("%s %s %s\n", package.runtime ? "runtime" : "app", package.package.c_str(),
                        package.latest_version.c_str());
        return 0;
    }
    if (argc == 2 && std::string(argv[1]) == "--output-limit-probe") {
        const std::string chunk(256, 'x');
        for (int index = 0; index < 4096; ++index)
            if (write(STDOUT_FILENO, chunk.data(), chunk.size()) != static_cast<ssize_t>(chunk.size())) return 1;
        return 37;
    }
    // Package operations must finish even if their diagnostic output is large.
    PosixProcessRunner bounded_runner(ProcessOutputLimit::truncate);
    const auto completed = bounded_runner.run({argv[0], "--output-limit-probe"}, 8);
    assert(completed.exit_code == 37);
    assert(completed.output_limit_exceeded);
    assert(completed.output == std::string(8, 'x'));

    ScopedEnvironmentVariable architecture("LILYGO_UI_STORE_ARCHITECTURE", nullptr);
    ScopedEnvironmentVariable appkit_version("LILYGO_UI_APPKIT_VERSION", nullptr);
    test_router();
    test_model();
    test_runtime_model_install_and_rollback_results();
    test_json_sha_and_debian_versions();
    test_registry_snapshot_protocol();
    test_store_service_and_package_verification();
    test_store_architecture_selection();
    test_runtime_catalog_and_updates();
    test_runtime_release_history_and_complete_catalog();
    test_local_runtimes_without_registry_metadata();
    test_runtime_inventory_detects_components_without_launchers();
    test_package_architecture_detection_and_installation();
    test_github_api_package_download();
    test_registry_root_cache();
    test_image_download_validation_and_curl_options();
    return 0;
}
