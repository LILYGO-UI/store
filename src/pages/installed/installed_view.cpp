#include "pages/installed/installed_view.hpp"

#include "components/components.hpp"

#include <cm0/typography.h>

#include <cstdint>
#include <cstdio>

namespace {

void create_summary(lv_obj_t *body, const InstalledViewModel &view_model)
{
    auto *summary = make_flex(body, LV_FLEX_FLOW_ROW);
    lv_obj_set_name_static(summary, "installed_summary");
    lv_obj_set_size(summary, LV_PCT(100), 44);
    lv_obj_set_flex_align(summary, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    char count[40]{};
    const auto installed_count_value = view_model.installed_count();
    std::snprintf(count, sizeof(count), "%zu app%s installed", installed_count_value,
                  installed_count_value == 1 ? "" : "s");
    auto *installed_count_label = make_label(summary, count, 14, COLOR_SECONDARY);
    lv_obj_set_width(installed_count_label, LV_PCT(48));
    lv_label_set_long_mode(installed_count_label, LV_LABEL_LONG_DOT);

    char updates[40]{};
    const auto update_count_value = view_model.update_count();
    std::snprintf(updates, sizeof(updates), "%zu update%s available", update_count_value,
                  update_count_value == 1 ? "" : "s");
    auto *update_count_label = make_label(summary, updates, 14, update_count_value ? COLOR_ACCENT : COLOR_SECONDARY);
    lv_obj_set_width(update_count_label, LV_PCT(48));
    lv_obj_set_style_text_align(update_count_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(update_count_label, LV_LABEL_LONG_DOT);
}

void create_installed_row(lv_obj_t *body, std::size_t index, const StoreApp &app,
                          const InstalledViewCallbacks &callbacks)
{
    auto *row = lv_button_create(body);
    lv_obj_set_name(row, app.id.c_str());
    style_box(row, COLOR_SURFACE, 8);
    lv_obj_set_size(row, LV_PCT(100), 96);
    lv_obj_set_style_pad_all(row, 12, 0);
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(row, callbacks.select_app, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<std::uintptr_t>(index)));

    make_app_icon(row, app, index, 56);
    auto *copy = make_flex(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(copy, 0);
    lv_obj_set_height(copy, 58);
    lv_obj_set_flex_grow(copy, 1);
    lv_obj_set_style_pad_row(copy, 2, 0);
    auto *name = make_label(copy, app.name.c_str(), 22, COLOR_TEXT);
    lv_obj_set_width(name, LV_PCT(100));
    lv_obj_set_height(name, 30);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);

    char version[72]{};
    if (!app.operation_error.empty())
        std::snprintf(version, sizeof(version), "Action failed · View details");
    else if (app.update_available)
        std::snprintf(version, sizeof(version), "%s → %s · %s", app.version.c_str(), app.latest_version.c_str(),
                      app.size.c_str());
    else
        std::snprintf(version, sizeof(version), "Version %s · %s", app.version.c_str(), app.size.c_str());
    auto *metadata = make_label(
        copy, version, 14,
        !app.operation_error.empty() ? COLOR_DANGER : (app.update_available ? COLOR_WARNING : COLOR_SECONDARY));
    lv_obj_set_width(metadata, LV_PCT(100));
    lv_obj_set_height(metadata, 22);
    lv_label_set_long_mode(metadata, LV_LABEL_LONG_DOT);

    auto *actions = make_flex(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_name_static(actions, "installed_action_group");
    const bool removable = is_removable_package(app.package);
    lv_obj_set_size(actions, removable ? 116 : 64, 44);
    lv_obj_set_style_pad_column(actions, 8, 0);

    auto *primary = make_button(
        actions,
        app.busy ? "Working"
                 : (app.update_available ? "Update" : (is_launcher_package(app.package) ? "Details" : "Open")),
        14, COLOR_ACTION, COLOR_SURFACE, callbacks.primary_action,
        reinterpret_cast<void *>(static_cast<std::uintptr_t>(index)));
    lv_obj_set_size(primary, 64, 44);
    lv_obj_set_name_static(primary, "installed_primary_action");

    if (!removable) return;

    auto *remove = make_button(actions, LV_SYMBOL_TRASH, 22, COLOR_SURFACE, COLOR_DANGER, callbacks.remove,
                               reinterpret_cast<void *>(static_cast<std::uintptr_t>(index)));
    lv_obj_set_name_static(remove, "installed_remove_action");
    lv_obj_set_size(remove, 44, 44);
    lv_obj_set_style_pad_all(remove, 0, 0);
    lv_obj_set_style_border_width(remove, 1, 0);
    lv_obj_set_style_border_color(remove, lv_color_hex(COLOR_DANGER_BORDER), 0);
}

void create_storage(lv_obj_t *body)
{
    auto *heading = make_label(body, "Storage", 22, COLOR_TITLE);
    lv_obj_set_name_static(heading, "storage_heading");

    auto *card = make_card(body, 76);
    lv_obj_set_name_static(card, "storage_overview");
    lv_obj_set_style_pad_ver(card, 10, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto *copy = make_flex(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(copy, 0);
    lv_obj_set_height(copy, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(copy, 1);
    lv_obj_set_style_pad_row(copy, 2, 0);
    auto *used = make_label(copy, "Apps use 7.4 MB", 22, COLOR_TEXT);
    lv_obj_set_width(used, LV_PCT(100));
    lv_label_set_long_mode(used, LV_LABEL_LONG_DOT);
    auto *available = make_label(copy, "52.6 GB available", 14, COLOR_SECONDARY);
    lv_obj_set_width(available, LV_PCT(100));
    lv_label_set_long_mode(available, LV_LABEL_LONG_DOT);

    auto *status = make_button(card, "Plenty", 14, COLOR_SUCCESS_SOFT, COLOR_SUCCESS);
    lv_obj_set_size(status, 72, 36);
}

}  // namespace

namespace {

void build_installed_view(lv_obj_t *content, InstalledViewModel &view_model, const InstalledViewCallbacks &callbacks)
{
    make_header(content, "Installed", nullptr, "Manage");
    auto *body = make_page_body(content);
    create_summary(body, view_model);

    for (std::size_t index = 0; index < view_model.apps().size(); ++index) {
        const auto &app = view_model.apps()[index];
        if (app.installed) create_installed_row(body, index, app, callbacks);
    }

    if (view_model.installed_count() == 0) {
        auto *empty = make_flex(body, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_name_static(empty, "installed_empty_state");
        lv_obj_set_size(empty, LV_PCT(100), 180);
        lv_obj_set_style_pad_row(empty, 12, 0);
        lv_obj_set_flex_align(empty, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        make_label(empty, LV_SYMBOL_DOWNLOAD, 36, COLOR_SECONDARY);
        make_label(empty, "No apps installed", 22, COLOR_SECONDARY);
    }

    create_storage(body);
}

}  // namespace

InstalledView::InstalledView(InstalledViewModel &view_model, InstalledViewCallbacks callbacks) noexcept
    : view_model_(view_model), callbacks_(callbacks)
{
}

lv_obj_t *InstalledView::create(lv_obj_t *parent)
{
    destroy();
    surface_ = make_flex(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_name_static(surface_, "installed_page");
    lv_obj_set_size(surface_, LV_PCT(100), LV_PCT(100));
    rebuild();
    suppress_refresh_ = true;
    lv_subject_add_observer_obj(view_model_.revision_subject(), revision_changed, surface_, this);
    suppress_refresh_ = false;
    return surface_;
}

void InstalledView::destroy() noexcept
{
    lv_async_call_cancel(refresh_async, this);
    refresh_pending_ = false;
    if (surface_) lv_obj_delete(surface_);
    surface_ = nullptr;
}

void InstalledView::revision_changed(lv_observer_t *observer, lv_subject_t *)
{
    auto *view = static_cast<InstalledView *>(lv_observer_get_user_data(observer));
    if (view && !view->suppress_refresh_) view->request_refresh();
}

void InstalledView::refresh_async(void *user_data)
{
    auto *view             = static_cast<InstalledView *>(user_data);
    view->refresh_pending_ = false;
    if (view->surface_) view->rebuild();
}

void InstalledView::request_refresh() noexcept
{
    if (!surface_ || refresh_pending_) return;
    refresh_pending_ = lv_async_call(refresh_async, this) == LV_RESULT_OK;
}

void InstalledView::rebuild()
{
    int scroll_y = 0;
    if (auto *body = lv_obj_find_by_name(surface_, "page_body")) scroll_y = lv_obj_get_scroll_y(body);

    lv_obj_clean(surface_);
    build_installed_view(surface_, view_model_, callbacks_);
    if (callbacks_.layout_changed) callbacks_.layout_changed();
    lv_obj_update_layout(surface_);
    if (auto *body = lv_obj_find_by_name(surface_, "page_body")) lv_obj_scroll_to_y(body, scroll_y, LV_ANIM_OFF);
}
