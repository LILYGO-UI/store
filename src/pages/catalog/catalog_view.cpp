#include "pages/catalog/catalog_view.hpp"

#include "components/components.hpp"

#include <cm0/typography.h>

#include <cstdint>
#include <cstdio>

namespace {

void create_catalog_section_heading(lv_obj_t *body)
{
    auto *heading = make_flex(body, LV_FLEX_FLOW_ROW);
    lv_obj_set_name_static(heading, "catalog_section_heading");
    lv_obj_set_size(heading, LV_PCT(100), 32);
    lv_obj_set_flex_align(heading, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    make_label(heading, "All apps", 22, COLOR_TITLE);
    make_label(heading, "Ready to explore", 14, COLOR_SECONDARY);
}

void create_filter(lv_obj_t *body, CatalogViewModel &view_model, lv_event_cb_t callback)
{
    auto *control = make_flex(body, LV_FLEX_FLOW_ROW);
    lv_obj_set_name_static(control, "source_filter");
    lv_obj_set_size(control, LV_PCT(100), 48);
    style_box(control, COLOR_CONTROL, 8);
    lv_obj_set_style_pad_all(control, 4, 0);
    lv_obj_set_style_pad_column(control, 4, 0);

    struct FilterItem {
        CatalogFilter filter;
        const char *label;
        const char *object_name;
    };
    static constexpr FilterItem items[] = {
        {CatalogFilter::all, "All", "filter_all"},
        {CatalogFilter::official, "Official", "filter_official"},
        {CatalogFilter::community, "Community", "filter_community"},
    };

    for (const auto &item : items) {
        const bool selected = view_model.filter() == item.filter;
        auto *button        = make_button(control, item.label, 14, selected ? COLOR_SURFACE : COLOR_CONTROL,
                                   selected ? COLOR_TITLE : COLOR_DISABLED_TEXT, callback,
                                          reinterpret_cast<void *>(static_cast<std::intptr_t>(item.filter)));
        lv_obj_set_name_static(button, item.object_name);
        lv_obj_set_height(button, LV_PCT(100));
        lv_obj_set_flex_grow(button, 1);
        lv_obj_set_style_radius(button, 6, 0);
        lv_obj_set_style_pad_hor(button, 2, 0);
    }
}

void create_app_row(lv_obj_t *body, std::size_t index, const StoreApp &app, const CatalogViewCallbacks &callbacks)
{
    auto *row = lv_button_create(body);
    lv_obj_set_name(row, app.id.c_str());
    style_box(row, COLOR_SURFACE, 12);
    lv_obj_set_size(row, LV_PCT(100), 104);
    lv_obj_set_style_pad_all(row, 14, 0);
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(row, callbacks.select_app, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<std::uintptr_t>(index)));

    make_app_icon(row, app, index, 56);
    auto *copy = make_flex(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(copy, 0);
    lv_obj_set_height(copy, 64);
    lv_obj_set_flex_grow(copy, 1);
    lv_obj_set_style_pad_row(copy, 2, 0);

    auto *name = make_label(copy, app.name.c_str(), 22, COLOR_TEXT);
    lv_obj_set_width(name, LV_PCT(100));
    lv_obj_set_height(name, 30);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);

    char metadata[64]{};
    if (!app.operation_error.empty())
        std::snprintf(metadata, sizeof(metadata), "Action failed · View details");
    else {
        std::snprintf(metadata, sizeof(metadata), "%s · %s", app.official ? "Official" : "Community", app.size.c_str());
    }
    auto *meta = make_label(copy, metadata, 14, app.operation_error.empty() ? COLOR_SECONDARY : COLOR_DANGER);
    lv_obj_set_width(meta, LV_PCT(100));
    lv_obj_set_height(meta, 22);
    lv_label_set_long_mode(meta, LV_LABEL_LONG_DOT);

    const bool launcher_requires_baseline = is_launcher_package(app.package) && !app.installed;
    const char *status =
        app.busy
            ? "Working"
            : (launcher_requires_baseline
                   ? "System"
                   : (app.update_available ? "Update"
                                           : (app.installed ? "Installed" : (app.available ? "Get" : "Unavailable"))));
    const bool actionable =
        !app.busy && app.available && !launcher_requires_baseline && (!app.installed || app.update_available);
    auto *badge =
        make_button(row, status, 14, actionable ? COLOR_ACTION : COLOR_DISABLED_SURFACE,
                    actionable ? COLOR_SURFACE : COLOR_SECONDARY, actionable ? callbacks.quick_action : nullptr,
                    reinterpret_cast<void *>(static_cast<std::uintptr_t>(index)));
    lv_obj_set_name_static(badge, "catalog_primary_action");
    lv_obj_set_size(badge, 64, 44);
    if (!app.available)
        lv_obj_set_width(badge, 88);
    else if (app.installed && !app.update_available)
        lv_obj_set_width(badge, 72);
    if (actionable) lv_obj_add_flag(badge, LV_OBJ_FLAG_CLICKABLE);
}

void create_runtime_row(lv_obj_t *body, std::size_t index, const StoreApp &app, const CatalogViewCallbacks &callbacks)
{
    auto *row = lv_button_create(body);
    lv_obj_set_name(row, app.id.c_str());
    style_box(row, COLOR_SURFACE, 12);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(row, 14, 0);
    lv_obj_set_style_pad_row(row, 8, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_event_cb(row, callbacks.select_app, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<std::uintptr_t>(index)));

    auto *heading = make_flex(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_size(heading, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_column(heading, 8, 0);
    lv_obj_set_flex_align(heading, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    auto *name = make_label(heading, app.name.c_str(), 22, COLOR_TITLE);
    lv_obj_set_width(name, 0);
    lv_obj_set_flex_grow(name, 1);
    lv_label_set_long_mode(name, LV_LABEL_LONG_WRAP);
    const bool actionable = !app.busy && app.available && (!app.installed || app.update_available);
    const char *action =
        app.busy ? "Working"
                 : (!app.catalogued
                        ? "Unknown"
                        : (!app.available
                               ? "Unavailable"
                               : (!app.installed ? "Install" : (app.update_available ? "Update" : "Up to date"))));
    auto *button =
        make_button(heading, action, 14, actionable ? COLOR_ACTION : COLOR_DISABLED_SURFACE,
                    actionable ? COLOR_SURFACE : COLOR_SECONDARY, actionable ? callbacks.quick_action : nullptr,
                    reinterpret_cast<void *>(static_cast<std::uintptr_t>(index)));
    lv_obj_set_name_static(button, "runtime_update_action");
    lv_obj_set_size(button, 96, 44);
    for (const auto &text :
         {app.package, app.installed ? "Installed: " + app.version : "Not installed",
          !app.catalogued ? "Update information unavailable"
                          : (app.available ? "Available: " + app.latest_version : "No compatible release")}) {
        if (text == app.package && app.name == app.package) continue;
        auto *label = make_label(row, text.c_str(), 14, COLOR_SECONDARY);
        lv_obj_set_width(label, LV_PCT(100));
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    }
    if (!app.operation_error.empty()) {
        auto *error = make_label(row, "Update failed · View details", 14, COLOR_DANGER);
        lv_obj_set_width(error, LV_PCT(100));
        lv_label_set_long_mode(error, LV_LABEL_LONG_WRAP);
    }
}

void create_runtime_heading(lv_obj_t *body, CatalogViewModel &view_model, lv_event_cb_t refresh)
{
    auto *heading = make_flex(body, LV_FLEX_FLOW_ROW);
    lv_obj_set_size(heading, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_column(heading, 8, 0);
    lv_obj_set_flex_align(heading, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    auto *title = make_label(heading, "Runtime", 28, COLOR_TITLE);
    lv_obj_set_width(title, 0);
    lv_obj_set_flex_grow(title, 1);
    const bool loading = view_model.registry_state() == RegistryState::loading;
    auto *check = make_button(heading, loading ? "Checking..." : "Check updates", 14, COLOR_ACTION, COLOR_SURFACE,
                              loading ? nullptr : refresh);
    lv_obj_set_name_static(check, "runtime_check_updates");
    lv_obj_set_size(check, 120, 44);
    auto *description = make_label(body, "Browse runtime packages and manage installed versions.", 14, COLOR_SECONDARY);
    lv_obj_set_width(description, LV_PCT(100));
    lv_label_set_long_mode(description, LV_LABEL_LONG_WRAP);
    if (view_model.registry_state() == RegistryState::ready && !view_model.uncatalogued_count()) {
        const auto count = view_model.update_count();
        const auto text  = std::to_string(count) + (count == 1 ? " update available" : " updates available");
        make_label(body, text.c_str(), 14, count ? COLOR_ACCENT : COLOR_SECONDARY);
    }
    if (view_model.uncatalogued_count()) {
        auto *notice = make_label(body, "Some installed components have no update information in this catalog.", 14,
                                  COLOR_SECONDARY);
        lv_obj_set_name_static(notice, "runtime_catalog_notice");
        lv_obj_set_width(notice, LV_PCT(100));
        lv_label_set_long_mode(notice, LV_LABEL_LONG_WRAP);
    }
}

void create_registry_status(lv_obj_t *body, CatalogViewModel &view_model, lv_event_cb_t refresh)
{
    if (view_model.registry_state() == RegistryState::ready) return;
    const bool failed = view_model.registry_state() == RegistryState::error;
    auto *notice      = make_flex(body, LV_FLEX_FLOW_ROW);
    lv_obj_set_name_static(notice, "registry_status");
    lv_obj_set_width(notice, LV_PCT(100));
    lv_obj_set_height(notice, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(notice, 12, 0);
    lv_obj_set_style_pad_column(notice, 8, 0);
    style_box(notice, failed ? COLOR_DANGER_SOFT : COLOR_ACCENT_SOFT, 8);
    auto *message = make_label(notice, view_model.status_message().c_str(), 14, failed ? COLOR_DANGER : COLOR_TEXT);
    lv_obj_set_width(message, 0);
    lv_obj_set_flex_grow(message, 1);
    lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
    if (failed) {
        auto *button = make_button(notice, "Retry", 14, COLOR_SURFACE, COLOR_ACCENT, refresh);
        lv_obj_set_name_static(button, "registry_retry");
        lv_obj_set_size(button, 64, 40);
    }
}

}  // namespace

namespace {

void build_catalog_view(lv_obj_t *content, CatalogViewModel &view_model, const CatalogViewCallbacks &callbacks)
{
    auto *body = make_page_body(content);
    if (view_model.runtimes()) create_runtime_heading(body, view_model, callbacks.refresh);
    create_registry_status(body, view_model, callbacks.refresh);
    if (!view_model.runtimes()) {
        create_filter(body, view_model, callbacks.set_filter);
        create_catalog_section_heading(body);
    }

    auto *grid = make_flex(body, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_name_static(grid, "catalog_grid");
    lv_obj_set_size(grid, LV_PCT(100), LV_SIZE_CONTENT);
    // LVGL's keyboard-focus style draws an outline outside a button (the
    // default theme uses a three-pixel outline and pad). Keep that outline
    // inside the scroll body's clip rectangle when a full-width card is
    // focused with Tab. Without this inset, the grid clips the outer edge of
    // the first/last card's focus frame.
    lv_obj_set_style_pad_all(grid, 8, 0);
    lv_obj_set_style_pad_column(grid, 12, 0);
    lv_obj_set_style_pad_row(grid, 12, 0);

    bool has_visible_apps = false;
    for (std::size_t index = 0; index < view_model.apps().size(); ++index) {
        const auto &app = view_model.apps()[index];
        if (!view_model.is_visible(app)) continue;
        if (view_model.runtimes())
            create_runtime_row(grid, index, app, callbacks);
        else
            create_app_row(grid, index, app, callbacks);
        has_visible_apps = true;
    }

    if (view_model.registry_state() == RegistryState::ready && !has_visible_apps) {
        auto *empty = make_flex(body, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_name_static(empty, "catalog_empty_state");
        lv_obj_set_size(empty, LV_PCT(100), 180);
        lv_obj_set_style_pad_row(empty, 12, 0);
        lv_obj_set_flex_align(empty, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        make_label(empty, LV_SYMBOL_DOWNLOAD, 36, COLOR_SECONDARY);
        auto *text = make_label(empty, view_model.runtimes() ? "No runtime packages available" : "No apps available",
                                22, COLOR_SECONDARY);
        lv_obj_set_width(text, LV_PCT(100));
        lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
    }
}

}  // namespace

CatalogView::CatalogView(CatalogViewModel &view_model, CatalogViewCallbacks callbacks) noexcept
    : view_model_(view_model), callbacks_(callbacks)
{
}

lv_obj_t *CatalogView::create(lv_obj_t *parent)
{
    destroy();
    surface_ = make_flex(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_name_static(surface_, view_model_.runtimes() ? "runtime_page" : "catalog_page");
    lv_obj_set_size(surface_, LV_PCT(100), LV_PCT(100));
    rebuild();
    suppress_refresh_ = true;
    lv_subject_add_observer_obj(view_model_.revision_subject(), revision_changed, surface_, this);
    suppress_refresh_ = false;
    return surface_;
}

void CatalogView::destroy() noexcept
{
    lv_async_call_cancel(refresh_async, this);
    refresh_pending_ = false;
    if (surface_) lv_obj_delete(surface_);
    surface_ = nullptr;
}

void CatalogView::revision_changed(lv_observer_t *observer, lv_subject_t *)
{
    auto *view = static_cast<CatalogView *>(lv_observer_get_user_data(observer));
    if (view && !view->suppress_refresh_) view->request_refresh();
}

void CatalogView::refresh_async(void *user_data)
{
    auto *view             = static_cast<CatalogView *>(user_data);
    view->refresh_pending_ = false;
    if (view->surface_) view->rebuild();
}

void CatalogView::request_refresh() noexcept
{
    if (!surface_ || refresh_pending_) return;
    refresh_pending_ = lv_async_call(refresh_async, this) == LV_RESULT_OK;
}

void CatalogView::rebuild()
{
    int scroll_y = 0;
    if (auto *body = lv_obj_find_by_name(surface_, "page_body")) scroll_y = lv_obj_get_scroll_y(body);

    lv_obj_clean(surface_);
    build_catalog_view(surface_, view_model_, callbacks_);
    if (callbacks_.layout_changed) callbacks_.layout_changed();
    lv_obj_update_layout(surface_);
    if (auto *body = lv_obj_find_by_name(surface_, "page_body")) lv_obj_scroll_to_y(body, scroll_y, LV_ANIM_OFF);
}
