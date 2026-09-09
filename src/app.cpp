#include "app.hpp"

#include "app_identity.h"
#include "app_router.hpp"
#include "components/components.hpp"
#include "pages/catalog/catalog_view.hpp"
#include "pages/detail/detail_view.hpp"
#include "pages/installed/installed_view.hpp"
#include "service/store_service.hpp"
#include "store_view_model.hpp"

#include <cm0/typography.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr int kNavigationSize        = 64;
constexpr int kWideNavigationWidth   = 180;
constexpr int kContentMaxWidth       = 720;
constexpr int kHomeIndicatorSafeArea = 38;

enum class AsyncResultKind { refresh, install, remove, icon, screenshot };

struct AsyncResult {
    AsyncResultKind kind = AsyncResultKind::refresh;
    bool cached_refresh  = false;
    std::string app_id;
    bool was_update         = false;
    std::size_t image_index = 0;
    std::string image_url;
    std::string image_cache_key;
    StoreRefreshResult refresh;
    PackageOperationResult package;
    StoreImageDownloadResult image;
};

struct AsyncMailbox {
    std::mutex mutex;
    std::deque<AsyncResult> results;
    std::atomic<bool> accepting{true};
};

struct AppViewState {
    std::unique_ptr<StoreViewModel> store;
    AppRouter router;
    std::unique_ptr<CatalogViewModel> catalog;
    std::unique_ptr<InstalledViewModel> installed;
    std::unique_ptr<DetailViewModel> detail;
    std::unique_ptr<CatalogView> catalog_view;
    std::unique_ptr<InstalledView> installed_view;
    std::unique_ptr<DetailView> detail_view;
    lv_obj_t *surface                     = nullptr;
    lv_obj_t *content                     = nullptr;
    lv_obj_t *navigation                  = nullptr;
    lv_timer_t *worker_timer              = nullptr;
    std::shared_ptr<AsyncMailbox> mailbox = std::make_shared<AsyncMailbox>();
    bool refresh_running                  = false;
    bool online_refresh_finished          = false;
    bool online_refresh_succeeded         = false;
    std::string online_refresh_error;
    bool operation_running       = false;
    bool page_render_pending     = false;
    bool closing                 = false;
    void (*request_exit)(void *) = nullptr;
    void *request_exit_user_data = nullptr;

    void prepare_session()
    {
        if (mailbox) mailbox->accepting = false;
        router                   = AppRouter{};
        surface                  = nullptr;
        content                  = nullptr;
        navigation               = nullptr;
        worker_timer             = nullptr;
        mailbox                  = std::make_shared<AsyncMailbox>();
        refresh_running          = false;
        online_refresh_finished  = false;
        online_refresh_succeeded = false;
        online_refresh_error.clear();
        operation_running      = false;
        page_render_pending    = false;
        closing                = false;
        request_exit           = nullptr;
        request_exit_user_data = nullptr;
    }
};

AppViewState app_state;

void render_page_now();
void request_page_render();
void render_page_async(void *);
void apply_layout();
void start_registry_refresh(bool use_cache = false);
void start_app_icon_downloads();
void start_detail_screenshot_downloads();

std::string registry_root_url()
{
    if (const auto *configured = std::getenv("LILYGO_UI_STORE_REGISTRY_URL"); configured && *configured)
        return configured;
    return kDefaultRegistryRoot;
}

void deliver_async(const std::shared_ptr<AsyncMailbox> &mailbox, AsyncResult result)
{
    if (!mailbox->accepting) return;
    std::lock_guard<std::mutex> lock(mailbox->mutex);
    if (mailbox->accepting) mailbox->results.push_back(std::move(result));
}

void apply_async_result(AsyncResult result)
{
    if (!app_state.store || app_state.closing) return;
    if (result.kind == AsyncResultKind::refresh) {
        if (result.cached_refresh) {
            if (app_state.online_refresh_succeeded || !result.refresh) return;
            app_state.store->replace_catalog(std::move(result.refresh.apps));
            if (app_state.online_refresh_finished)
                app_state.store->set_registry_error(app_state.online_refresh_error);
            else
                app_state.store->begin_loading("Checking for catalog updates...");
            start_app_icon_downloads();
            return;
        }

        app_state.online_refresh_finished  = true;
        app_state.online_refresh_succeeded = static_cast<bool>(result.refresh);
        app_state.refresh_running          = false;
        if (result.refresh) {
            if (!result.refresh.cache_error.empty())
                LV_LOG_WARN("Unable to cache Registry root: %s", result.refresh.cache_error.c_str());
            app_state.store->replace_catalog(std::move(result.refresh.apps));
            start_app_icon_downloads();
        } else {
            app_state.online_refresh_error = result.refresh.error;
            app_state.store->set_registry_error(std::move(result.refresh.error));
        }
        if (app_state.router.page() == StorePage::detail) start_detail_screenshot_downloads();
        return;
    }

    if (result.kind == AsyncResultKind::icon) {
        app_state.store->complete_icon(result.app_id, result.image_url, result.image_cache_key,
                                       std::move(result.image.path), std::move(result.image.error));
        return;
    }

    if (result.kind == AsyncResultKind::screenshot) {
        app_state.store->complete_screenshot(result.app_id, result.image_index, result.image_url,
                                             result.image_cache_key, std::move(result.image.path),
                                             std::move(result.image.error));
        return;
    }

    app_state.operation_running = false;
    if (!result.package) {
        app_state.store->fail_operation(result.app_id, std::move(result.package.error));
        return;
    }
    if (result.package.launcher_update_queued) {
        if (app_state.request_exit)
            app_state.request_exit(app_state.request_exit_user_data);
        else
            LV_LOG_ERROR("Launcher update queued, but Store has no exit callback");
        return;
    }
    if (result.kind == AsyncResultKind::remove)
        app_state.store->complete_remove(result.app_id);
    else
        app_state.store->complete_install(result.app_id, result.was_update,
                                          std::move(result.package.installed_version));
}

void poll_workers(lv_timer_t *)
{
    std::deque<AsyncResult> ready;
    {
        std::lock_guard<std::mutex> lock(app_state.mailbox->mutex);
        ready.swap(app_state.mailbox->results);
    }
    while (!ready.empty()) {
        auto result = std::move(ready.front());
        ready.pop_front();
        apply_async_result(std::move(result));
    }
}

void start_registry_refresh(bool use_cache)
{
    if (!app_state.store || app_state.refresh_running || app_state.operation_running) return;
    app_state.refresh_running          = true;
    app_state.online_refresh_finished  = false;
    app_state.online_refresh_succeeded = false;
    app_state.online_refresh_error.clear();
    app_state.store->begin_loading();
    const auto mailbox = app_state.mailbox;
    const auto url     = registry_root_url();
    if (use_cache) {
        std::thread([mailbox, url] {
            AsyncResult result;
            result.kind           = AsyncResultKind::refresh;
            result.cached_refresh = true;
            result.refresh        = refresh_cached_store(url);
            deliver_async(mailbox, std::move(result));
        }).detach();
    }
    std::thread([mailbox, url] {
        AsyncResult result;
        result.kind    = AsyncResultKind::refresh;
        result.refresh = refresh_store(url);
        deliver_async(mailbox, std::move(result));
    }).detach();
}

void start_app_icon_downloads()
{
    if (!app_state.store) return;
    auto pending = app_state.store->begin_icon_downloads();
    if (pending.empty()) return;

    const auto mailbox = app_state.mailbox;
    std::thread([mailbox, pending = std::move(pending)] {
        for (const auto &icon : pending) {
            AsyncResult result;
            result.kind            = AsyncResultKind::icon;
            result.app_id          = icon.app_id;
            result.image_url       = icon.url;
            result.image_cache_key = icon.cache_key;
            result.image           = download_store_image(icon.url, icon.cache_key);
            deliver_async(mailbox, std::move(result));
            if (!mailbox->accepting) return;
        }
    }).detach();
}

void start_detail_screenshot_downloads()
{
    if (!app_state.store || !app_state.detail) return;
    auto pending = app_state.store->begin_screenshot_downloads(app_state.detail->app_index());
    if (pending.empty()) return;

    const auto mailbox = app_state.mailbox;
    std::thread([mailbox, pending = std::move(pending)] {
        for (const auto &screenshot : pending) {
            AsyncResult result;
            result.kind            = AsyncResultKind::screenshot;
            result.app_id          = screenshot.app_id;
            result.image_index     = screenshot.index;
            result.image_url       = screenshot.url;
            result.image_cache_key = screenshot.cache_key;
            result.image           = download_store_image(screenshot.url, screenshot.cache_key);
            deliver_async(mailbox, std::move(result));
            if (!mailbox->accepting) return;
        }
    }).detach();
}

void start_install(std::size_t index)
{
    if (!app_state.store) return;
    auto command = app_state.store->begin_install(index, app_state.operation_running, app_state.refresh_running);
    if (!command) return;
    app_state.operation_running = true;
    const auto mailbox          = app_state.mailbox;
    std::thread([mailbox, command = std::move(*command)] {
        AsyncResult result;
        result.kind       = AsyncResultKind::install;
        result.app_id     = command.app_id;
        result.was_update = command.was_update;
        result.package    = install_store_package(command.package, command.release);
        deliver_async(mailbox, std::move(result));
    }).detach();
}

void start_remove(std::size_t index)
{
    if (!app_state.store) return;
    auto command = app_state.store->begin_remove(index, app_state.operation_running, app_state.refresh_running);
    if (!command) return;
    app_state.operation_running = true;
    const auto mailbox          = app_state.mailbox;
    std::thread([mailbox, command = std::move(*command)] {
        AsyncResult result;
        result.kind    = AsyncResultKind::remove;
        result.app_id  = command.app_id;
        result.package = remove_store_package(command.package);
        deliver_async(mailbox, std::move(result));
    }).detach();
}

std::size_t event_index(lv_event_t *event)
{
    return static_cast<std::size_t>(reinterpret_cast<std::uintptr_t>(lv_event_get_user_data(event)));
}

void select_app_event(lv_event_t *event)
{
    if (!app_state.catalog || !app_state.catalog->select(event_index(event))) return;
    app_state.router.show_detail();
    start_detail_screenshot_downloads();
    request_page_render();
}

void catalog_quick_action_event(lv_event_t *event)
{
    start_install(event_index(event));
}

void filter_event(lv_event_t *event)
{
    const auto filter = static_cast<CatalogFilter>(reinterpret_cast<std::intptr_t>(lv_event_get_user_data(event)));
    if (app_state.catalog) app_state.catalog->set_filter(filter);
}

void installed_primary_action_event(lv_event_t *event)
{
    const auto index = event_index(event);
    const auto *app  = app_state.store ? app_state.store->app(index) : nullptr;
    if (!app) return;
    if (app->update_available) {
        start_install(index);
        return;
    }
    if (app_state.installed && app_state.installed->select(index)) {
        app_state.router.show_detail();
        start_detail_screenshot_downloads();
        request_page_render();
    }
}

void installed_remove_event(lv_event_t *event)
{
    start_remove(event_index(event));
}

void navigation_event(lv_event_t *event)
{
    const auto page = static_cast<StorePage>(reinterpret_cast<std::intptr_t>(lv_event_get_user_data(event)));
    if (page == StorePage::catalog)
        app_state.router.show_catalog();
    else
        app_state.router.show_installed();
    request_page_render();
}

void back_event(lv_event_t *)
{
    if (app_state.router.back()) request_page_render();
}

void detail_primary_action_event(lv_event_t *)
{
    if (app_state.detail) start_install(app_state.detail->app_index());
}

void detail_remove_event(lv_event_t *)
{
    if (app_state.detail) start_remove(app_state.detail->app_index());
}

void refresh_event(lv_event_t *)
{
    start_registry_refresh();
}

StorePage active_navigation_page()
{
    return app_state.router.page() == StorePage::detail ? app_state.router.detail_origin() : app_state.router.page();
}

void create_navigation()
{
    lv_obj_clean(app_state.navigation);
    struct NavItem {
        StorePage page;
        const char *icon;
        const char *label;
    };
    static constexpr NavItem items[] = {
        {StorePage::catalog, LV_SYMBOL_DOWNLOAD, "Store"},
        {StorePage::installed, "\xEF\x80\x89", "Installed"},
    };

    for (const auto &item : items) {
        const bool selected = active_navigation_page() == item.page;
        auto *button        = lv_button_create(app_state.navigation);
        lv_obj_set_name_static(button, item.page == StorePage::catalog ? "navigation_store" : "navigation_installed");
        style_box(button, selected ? COLOR_ACCENT_SOFT : COLOR_SURFACE, 8);
        lv_obj_set_style_pad_all(button, 4, 0);
        lv_obj_set_style_pad_row(button, 2, 0);
        lv_obj_set_style_pad_column(button, 10, 0);
        lv_obj_set_flex_flow(button, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(button, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_event_cb(button, navigation_event, LV_EVENT_CLICKED,
                            reinterpret_cast<void *>(static_cast<std::intptr_t>(item.page)));
        auto *icon = make_label(button, item.icon, 22, selected ? COLOR_ACCENT : COLOR_SECONDARY);
        lv_obj_set_name_static(icon, "navigation_icon");
        auto *label = make_label(button, item.label, 14, selected ? COLOR_ACCENT : COLOR_SECONDARY);
        lv_obj_set_name_static(label, "navigation_label");
    }
}

void render_page_now()
{
    if (app_state.closing || !app_state.content || !app_state.navigation) return;

    if (app_state.catalog_view) app_state.catalog_view->destroy();
    if (app_state.installed_view) app_state.installed_view->destroy();
    if (app_state.detail_view) app_state.detail_view->destroy();

    switch (app_state.router.page()) {
        case StorePage::catalog:
            app_state.catalog_view->create(app_state.content);
            break;
        case StorePage::installed:
            app_state.installed_view->create(app_state.content);
            break;
        case StorePage::detail:
            app_state.detail_view->create(app_state.content);
            break;
    }

    create_navigation();
    apply_layout();
}

void request_page_render()
{
    if (app_state.closing || app_state.page_render_pending || !app_state.content) return;
    app_state.page_render_pending = lv_async_call(render_page_async, &app_state) == LV_RESULT_OK;
}

void render_page_async(void *user_data)
{
    auto *state                = static_cast<AppViewState *>(user_data);
    state->page_render_pending = false;
    if (!state->closing) render_page_now();
}

void apply_layout()
{
    if (!app_state.surface || !app_state.content || !app_state.navigation) return;

    const auto width = lv_obj_get_content_width(app_state.surface);
    const bool wide  = width >= 720;
    lv_obj_set_flex_flow(app_state.surface, wide ? LV_FLEX_FLOW_ROW : LV_FLEX_FLOW_COLUMN);

    lv_obj_set_flex_align(app_state.content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    auto *body = lv_obj_find_by_name(app_state.content, "page_body");
    if (body) {
        const bool detail = app_state.router.page() == StorePage::detail;
        lv_obj_set_width(body, LV_PCT(100));
        lv_obj_set_style_max_width(body, kContentMaxWidth, 0);
        lv_obj_set_style_pad_hor(body, 16, 0);
        lv_obj_set_style_pad_ver(body, detail ? 20 : 16, 0);
        lv_obj_set_style_pad_row(body, detail ? 16 : 12, 0);
    }

    if (wide) {
        lv_obj_move_to_index(app_state.navigation, 0);
        lv_obj_set_size(app_state.navigation, kWideNavigationWidth, LV_PCT(100));
        lv_obj_set_flex_grow(app_state.navigation, 0);
        lv_obj_set_flex_flow(app_state.navigation, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(app_state.navigation, 8, 0);
        lv_obj_set_style_pad_row(app_state.navigation, 8, 0);
        lv_obj_set_style_border_side(app_state.navigation, LV_BORDER_SIDE_RIGHT, 0);

        lv_obj_set_width(app_state.content, 0);
        lv_obj_set_height(app_state.content, LV_PCT(100));
        lv_obj_set_flex_grow(app_state.content, 1);

        for (std::uint32_t index = 0; index < lv_obj_get_child_count(app_state.navigation); ++index) {
            auto *button = lv_obj_get_child(app_state.navigation, index);
            lv_obj_set_size(button, LV_PCT(100), kNavigationSize);
            lv_obj_set_flex_grow(button, 0);
            lv_obj_set_flex_flow(button, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(button, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_hor(button, 16, 0);
        }
    } else {
        lv_obj_move_to_index(app_state.content, 0);
        lv_obj_set_size(app_state.content, LV_PCT(100), 0);
        lv_obj_set_flex_grow(app_state.content, 1);

        lv_obj_set_size(app_state.navigation, LV_PCT(100), kNavigationSize);
        lv_obj_set_flex_grow(app_state.navigation, 0);
        lv_obj_set_flex_flow(app_state.navigation, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_all(app_state.navigation, 0, 0);
        lv_obj_set_style_pad_column(app_state.navigation, 0, 0);
        lv_obj_set_style_border_side(app_state.navigation, LV_BORDER_SIDE_TOP, 0);

        for (std::uint32_t index = 0; index < lv_obj_get_child_count(app_state.navigation); ++index) {
            auto *button = lv_obj_get_child(app_state.navigation, index);
            lv_obj_set_width(button, LV_SIZE_CONTENT);
            lv_obj_set_height(button, LV_PCT(100));
            lv_obj_set_flex_grow(button, 1);
            lv_obj_set_flex_flow(button, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(button, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_hor(button, 4, 0);
        }
    }
}

void size_changed_event(lv_event_t *)
{
    apply_layout();
}

void app_close();

void app_open(const cm0_app_context_t *context)
{
    if (app_state.surface || app_state.store) app_close();
    app_state.prepare_session();
    app_state.request_exit           = context->request_exit;
    app_state.request_exit_user_data = context->user_data;
    if (!store_fonts_available()) {
        LV_LOG_ERROR("Store cannot open because AppKit fonts are unavailable");
        app_state.closing = true;
        return;
    }
    app_state.store        = std::make_unique<StoreViewModel>();
    app_state.catalog      = std::make_unique<CatalogViewModel>(*app_state.store);
    app_state.installed    = std::make_unique<InstalledViewModel>(*app_state.store);
    app_state.detail       = std::make_unique<DetailViewModel>(*app_state.store);
    app_state.catalog_view = std::make_unique<CatalogView>(
        *app_state.catalog,
        CatalogViewCallbacks{select_app_event, catalog_quick_action_event, filter_event, refresh_event, apply_layout});
    app_state.installed_view = std::make_unique<InstalledView>(
        *app_state.installed,
        InstalledViewCallbacks{select_app_event, installed_primary_action_event, installed_remove_event, apply_layout});
    app_state.detail_view = std::make_unique<DetailView>(
        *app_state.detail,
        DetailViewCallbacks{back_event, detail_primary_action_event, detail_remove_event, apply_layout});

    app_state.surface = lv_obj_create(context->root);
    lv_obj_set_name_static(app_state.surface, "app_surface");
    lv_obj_set_size(app_state.surface, LV_PCT(100), LV_PCT(100));
    style_box(app_state.surface, COLOR_SURFACE);
    lv_obj_set_style_pad_all(app_state.surface, 0, 0);
    lv_obj_set_style_pad_bottom(app_state.surface, kHomeIndicatorSafeArea, 0);
    lv_obj_set_style_pad_row(app_state.surface, 0, 0);
    lv_obj_set_style_pad_column(app_state.surface, 0, 0);
    lv_obj_clear_flag(app_state.surface, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(app_state.surface, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_event_cb(app_state.surface, size_changed_event, LV_EVENT_SIZE_CHANGED, nullptr);

    app_state.content = lv_obj_create(app_state.surface);
    lv_obj_set_name_static(app_state.content, "app_content");
    style_box(app_state.content, COLOR_PAGE);
    lv_obj_set_width(app_state.content, LV_PCT(100));
    lv_obj_set_flex_grow(app_state.content, 1);
    lv_obj_set_style_pad_all(app_state.content, 0, 0);
    lv_obj_set_style_pad_row(app_state.content, 0, 0);
    lv_obj_set_style_pad_column(app_state.content, 0, 0);
    lv_obj_clear_flag(app_state.content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(app_state.content, LV_FLEX_FLOW_COLUMN);

    app_state.navigation = lv_obj_create(app_state.surface);
    lv_obj_set_name_static(app_state.navigation, "app_navigation");
    style_box(app_state.navigation, COLOR_SURFACE);
    lv_obj_set_style_border_color(app_state.navigation, lv_color_hex(COLOR_SEPARATOR), 0);
    lv_obj_set_style_border_width(app_state.navigation, 1, 0);
    lv_obj_clear_flag(app_state.navigation, LV_OBJ_FLAG_SCROLLABLE);

    render_page_now();
    app_state.worker_timer = lv_timer_create(poll_workers, 100, nullptr);
    start_registry_refresh(true);
}

void app_close()
{
    app_state.closing = true;
    if (app_state.mailbox) app_state.mailbox->accepting = false;
    lv_async_call_cancel(render_page_async, &app_state);
    app_state.page_render_pending = false;
    if (app_state.worker_timer) lv_timer_delete(app_state.worker_timer);
    app_state.worker_timer = nullptr;
    if (app_state.catalog_view) app_state.catalog_view->destroy();
    if (app_state.installed_view) app_state.installed_view->destroy();
    if (app_state.detail_view) app_state.detail_view->destroy();
    app_state.catalog_view.reset();
    app_state.installed_view.reset();
    app_state.detail_view.reset();
    if (app_state.surface) lv_obj_delete(app_state.surface);
    app_state.surface    = nullptr;
    app_state.content    = nullptr;
    app_state.navigation = nullptr;
    app_state.catalog.reset();
    app_state.installed.reset();
    app_state.detail.reset();
    app_state.store.reset();
    app_state.router                   = AppRouter{};
    app_state.refresh_running          = false;
    app_state.online_refresh_finished  = false;
    app_state.online_refresh_succeeded = false;
    app_state.online_refresh_error.clear();
    app_state.operation_running      = false;
    app_state.request_exit           = nullptr;
    app_state.request_exit_user_data = nullptr;
}

const cm0_app_descriptor_t app_descriptor{CM0_APP_API_VERSION, sizeof(cm0_app_descriptor_t), LILYGO_UI_STORE_APP_ID,
                                          app_open, app_close};

}  // namespace

extern "C" const cm0_app_descriptor_t *cm0_app_get_descriptor()
{
    return &app_descriptor;
}
