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

Fixture make_registry_fixture(const std::string &source_repo = "https://github.com/LILYGO-UI/demo")
{
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
        "\"releases\":[{"
        "\"version\":\"1.2.3\","
        "\"architecture\":\"arm64\","
        "\"status\":\"published\","
        "\"size\":7,"
        "\"sha256\":"
        "\"239f59ed55e737c77147cf55ad0c1b030b6d7ee748a7426952f9b852d5a935e5\","
        "\"url\":\"https://github.com/LILYGO-UI/packages/releases/download/"
        "apt-pool-lilygo-ui-demo/sha256-test.deb\","
        "\"min_appkit_version\":\"0.1.0\","
        "\"published_at\":\"2026-08-31T12:00:00+08:00\""
        "}]}";
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
        "\"latest_version\":\"1.2.3\","
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
    std::vector<std::vector<std::string>> calls;
    std::string install_package = "lilygo-ui-demo";
    std::string install_version = "1.2.3";
    std::string launcher_handoff_content;
    std::string package_install_error;
    std::string installed_status = "ii ";

    ProcessResult run(const std::vector<std::string> &arguments, std::size_t) override
    {
        calls.push_back(arguments);
        if (arguments.size() >= 2 && arguments[0] == "dpkg" && arguments[1] == "--print-architecture")
            return {0, "arm64\n", {}, false};
        if (!arguments.empty() && arguments[0] == "dpkg-query") {
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
    assert(refreshed.apps.size() == 1);
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
    assert(community_refreshed.apps.size() == 1);
    assert(!community_refreshed.apps.front().official);

    auto traversal_fixture         = make_registry_fixture("https://github.com/LILYGO-UI/../attacker");
    community_http.documents       = traversal_fixture.documents;
    const auto traversal_refreshed = community_service.refresh(traversal_fixture.root_url);
    assert(traversal_refreshed);
    assert(traversal_refreshed.apps.size() == 1);
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
    const auto call_count              = runner.calls.size();
    const auto rejected                = packages.install("lilygo-ui-demo", bad_release);
    assert(!rejected);
    assert(rejected.error.find("SHA-256") != std::string::npos);
    assert(runner.calls.size() == call_count);

    auto wrong_size = refreshed.apps.front().release;
    wrong_size.size += 1;
    const auto size_call_count = runner.calls.size();
    const auto size_rejected   = packages.install("lilygo-ui-demo", wrong_size);
    assert(!size_rejected);
    assert(size_rejected.error.find("size") != std::string::npos);
    assert(runner.calls.size() == size_call_count);

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

    test_router();
    test_model();
    test_json_sha_and_debian_versions();
    test_registry_snapshot_protocol();
    test_store_service_and_package_verification();
    test_github_api_package_download();
    test_registry_root_cache();
    test_image_download_validation_and_curl_options();
    return 0;
}
