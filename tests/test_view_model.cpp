#include "pages/catalog/catalog_view_model.hpp"
#include "pages/detail/detail_view_model.hpp"
#include "pages/installed/installed_view_model.hpp"
#include "store_view_model.hpp"

#include <cassert>
#include <cstdint>
#include <string>
#include <utility>

namespace {

StoreApp make_app(std::string id, bool installed = false, bool update_available = false)
{
    StoreApp app;
    app.id               = std::move(id);
    app.app_id           = "cc.lilygo.ui." + app.id;
    app.package          = "lilygo-ui-" + app.id;
    app.name             = app.id;
    app.version          = installed ? "1.0.0" : "1.2.0";
    app.latest_version   = "1.2.0";
    app.size             = "1.0 MB";
    app.available        = true;
    app.installed        = installed;
    app.update_available = update_available;
    app.release.version  = "1.2.0";
    app.release.status   = "published";
    return app;
}

void assert_revision(lv_subject_t *subject, std::int32_t expected)
{
    assert(lv_subject_get_int(subject) == expected);
}

}  // namespace

int main()
{
    lv_init();
    {
        auto official           = make_app("Official");
        official.official       = true;
        official.icon_url       = "https://registry.example/official.png";
        official.icon_cache_key = "official-icon";
        official.screenshots.push_back(
            {"https://registry.example/official-preview.png", "Preview", "official-preview", {}, {}, false});
        auto community = make_app("Community", true, true);
        StoreViewModel store(StoreModel({official, community}));
        CatalogViewModel catalog(store);
        InstalledViewModel installed(store);
        DetailViewModel detail(store);

        auto *revision = store.revision_subject();
        assert_revision(revision, 0);
        catalog.set_filter(CatalogFilter::official);
        assert_revision(revision, 1);
        assert(catalog.is_visible(catalog.apps()[0]));
        assert(!catalog.is_visible(catalog.apps()[1]));

        assert(catalog.select(0));
        assert_revision(revision, 2);
        assert(detail.app() && detail.app()->id == "Official");

        store.begin_loading("Refreshing fixture");
        assert(store.model().registry_state() == RegistryState::loading);
        assert(store.model().status_message() == "Refreshing fixture");
        assert_revision(revision, 3);

        store.set_registry_error("Registry unavailable");
        assert(store.model().registry_state() == RegistryState::error);
        assert(store.model().status_message() == "Registry unavailable");
        assert_revision(revision, 4);

        store.replace_catalog({official, community}, "Catalog ready");
        assert(store.model().registry_state() == RegistryState::ready);
        assert(store.model().status_message() == "Catalog ready");
        assert(detail.app() && detail.app()->id == "Official");
        assert_revision(revision, 5);

        const auto icons = store.begin_icon_downloads();
        assert(icons.size() == 1 && icons[0].app_id == official.app_id);
        assert(store.app(0)->icon_loading);
        assert_revision(revision, 6);
        assert(store.begin_icon_downloads().empty());
        assert_revision(revision, 6);
        assert(!store.complete_icon(official.app_id, "wrong-url", official.icon_cache_key, "/tmp/icon.png", {}));
        assert_revision(revision, 6);
        assert(store.complete_icon(official.app_id, official.icon_url, official.icon_cache_key, "/tmp/icon.png", {}));
        assert(store.app(0)->icon_local_path == "/tmp/icon.png");
        assert_revision(revision, 7);

        const auto screenshots = store.begin_screenshot_downloads(0);
        assert(screenshots.size() == 1 && screenshots[0].index == 0);
        assert(store.app(0)->screenshots[0].loading);
        assert_revision(revision, 8);
        assert(!store.complete_screenshot(official.app_id, 0, "wrong-url", "official-preview", "/tmp/preview.png", {}));
        assert_revision(revision, 8);
        assert(store.complete_screenshot(official.app_id, 0, official.screenshots[0].url,
                                         official.screenshots[0].cache_key, "/tmp/preview.png", {}));
        assert(store.app(0)->screenshots[0].local_path == "/tmp/preview.png");
        assert_revision(revision, 9);

        const auto install = store.begin_install(0, false, false);
        assert(install && install->package == "lilygo-ui-Official");
        assert(store.app(0)->busy);
        assert_revision(revision, 10);
        store.fail_operation(install->app_id, "Install failed");
        assert(!store.app(0)->busy);
        assert(store.app(0)->operation_error == "Install failed");
        assert_revision(revision, 11);

        const auto retry = store.begin_install(0, false, false);
        assert(retry && store.app(0)->operation_error.empty());
        assert_revision(revision, 12);
        store.complete_install(retry->app_id, false, "1.2.0");
        assert(store.app(0)->installed);
        assert(installed.installed_count() == 2);
        assert_revision(revision, 13);

        const auto remove = store.begin_remove(0, false, false);
        assert(remove);
        assert_revision(revision, 14);
        store.complete_remove(remove->app_id);
        assert(!store.app(0)->installed);
        assert_revision(revision, 15);

        const auto update = store.begin_install(1, false, false);
        assert(update && update->was_update);
        assert_revision(revision, 16);
        store.complete_install(update->app_id, true, "1.2.0");
        assert(store.app(1)->installed && !store.app(1)->update_available);
        assert_revision(revision, 17);
    }
    {
        auto launcher    = make_app("Launcher", true, true);
        launcher.package = "lilygo-ui-launcher";
        StoreViewModel store(StoreModel({launcher}));

        const auto update = store.begin_install(0, false, false);
        assert(update && update->was_update);
        store.fail_operation(update->app_id, "fixture reset");
        assert(!store.begin_remove(0, false, false));

        launcher.installed        = false;
        launcher.update_available = false;
        StoreViewModel missing_baseline(StoreModel({launcher}));
        assert(!missing_baseline.begin_install(0, false, false));
    }
    lv_deinit();
    return 0;
}
