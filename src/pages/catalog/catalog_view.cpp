#include "pages/catalog/catalog_view.hpp"

#include "app_identity.h"
#include "components/components.hpp"

#include <cm0/typography.h>

#include <cstdint>
#include <cstdio>

namespace {

void create_filter(lv_obj_t *body, CatalogViewModel &view_model,
                   lv_event_cb_t callback) {
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
    auto *button = make_button(
        control, item.label, 14, selected ? COLOR_SURFACE : COLOR_CONTROL,
        selected ? COLOR_TITLE : COLOR_DISABLED_TEXT, callback,
        reinterpret_cast<void *>(static_cast<std::intptr_t>(item.filter)));
    lv_obj_set_name_static(button, item.object_name);
    lv_obj_set_height(button, LV_PCT(100));
    lv_obj_set_flex_grow(button, 1);
    lv_obj_set_style_radius(button, 6, 0);
    lv_obj_set_style_pad_hor(button, 2, 0);
  }
}

void create_app_row(lv_obj_t *body, std::size_t index, const StoreApp &app,
                    const CatalogViewCallbacks &callbacks) {
  auto *row = lv_button_create(body);
  lv_obj_set_name(row, app.id.c_str());
  style_box(row, COLOR_SURFACE, 8);
  lv_obj_set_size(row, LV_PCT(100), 88);
  lv_obj_set_style_pad_all(row, 12, 0);
  lv_obj_set_style_pad_column(row, 12, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_add_event_cb(
      row, callbacks.select_app, LV_EVENT_CLICKED,
      reinterpret_cast<void *>(static_cast<std::uintptr_t>(index)));

  make_app_icon(row, app, index, 56);
  auto *copy = make_flex(row, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_width(copy, 0);
  lv_obj_set_height(copy, 56);
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
    std::snprintf(metadata, sizeof(metadata), "%s · %s",
                  app.official ? "Official" : "Community", app.size.c_str());
  }
  auto *meta =
      make_label(copy, metadata, 14,
                 app.operation_error.empty() ? COLOR_SECONDARY : COLOR_DANGER);
  lv_obj_set_width(meta, LV_PCT(100));
  lv_obj_set_height(meta, 22);
  lv_label_set_long_mode(meta, LV_LABEL_LONG_DOT);

  const char *status =
      app.busy
          ? "Working"
          : (app.update_available
                 ? "Update"
                 : (app.installed ? "Installed"
                                  : (app.available ? "Get" : "Unavailable")));
  const bool actionable =
      !app.busy && app.available && (!app.installed || app.update_available);
  auto *badge = make_button(
      row, status, 14, actionable ? COLOR_ACTION : COLOR_DISABLED_SURFACE,
      actionable ? COLOR_SURFACE : COLOR_SECONDARY,
      actionable ? callbacks.quick_action : nullptr,
      reinterpret_cast<void *>(static_cast<std::uintptr_t>(index)));
  lv_obj_set_name_static(badge, "catalog_primary_action");
  lv_obj_set_size(badge, 64, 44);
  if (!app.available)
    lv_obj_set_width(badge, 88);
  else if (app.installed && !app.update_available)
    lv_obj_set_width(badge, 72);
  if (actionable)
    lv_obj_add_flag(badge, LV_OBJ_FLAG_CLICKABLE);
}

void create_registry_status(lv_obj_t *body, CatalogViewModel &view_model,
                            lv_event_cb_t refresh) {
  if (view_model.registry_state() == RegistryState::ready)
    return;
  const bool failed = view_model.registry_state() == RegistryState::error;
  auto *notice = make_flex(body, LV_FLEX_FLOW_ROW);
  lv_obj_set_name_static(notice, "registry_status");
  lv_obj_set_width(notice, LV_PCT(100));
  lv_obj_set_height(notice, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(notice, 12, 0);
  lv_obj_set_style_pad_column(notice, 8, 0);
  style_box(notice, failed ? COLOR_DANGER_SOFT : COLOR_ACCENT_SOFT, 8);
  auto *message = make_label(notice, view_model.status_message().c_str(), 14,
                             failed ? COLOR_DANGER : COLOR_TEXT);
  lv_obj_set_width(message, 0);
  lv_obj_set_flex_grow(message, 1);
  lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
  if (failed) {
    auto *button =
        make_button(notice, "Retry", 14, COLOR_SURFACE, COLOR_ACCENT, refresh);
    lv_obj_set_name_static(button, "registry_retry");
    lv_obj_set_size(button, 64, 40);
  }
}

} // namespace

namespace {

void build_catalog_view(lv_obj_t *content, CatalogViewModel &view_model,
                        const CatalogViewCallbacks &callbacks) {
  make_header(content, LILYGO_UI_STORE_APP_NAME);
  auto *body = make_page_body(content);
  create_registry_status(body, view_model, callbacks.refresh);
  create_filter(body, view_model, callbacks.set_filter);

  for (std::size_t index = 0; index < view_model.apps().size(); ++index) {
    const auto &app = view_model.apps()[index];
    if (view_model.is_visible(app))
      create_app_row(body, index, app, callbacks);
  }

  if (view_model.registry_state() == RegistryState::ready &&
      view_model.apps().empty()) {
    auto *empty = make_flex(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_name_static(empty, "catalog_empty_state");
    lv_obj_set_size(empty, LV_PCT(100), 180);
    lv_obj_set_style_pad_row(empty, 12, 0);
    lv_obj_set_flex_align(empty, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    make_label(empty, LV_SYMBOL_DOWNLOAD, 36, COLOR_SECONDARY);
    make_label(empty, "No apps available", 22, COLOR_SECONDARY);
  }
}

} // namespace

CatalogView::CatalogView(CatalogViewModel &view_model,
                         CatalogViewCallbacks callbacks) noexcept
    : view_model_(view_model), callbacks_(callbacks) {}

lv_obj_t *CatalogView::create(lv_obj_t *parent) {
  destroy();
  surface_ = make_flex(parent, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_name_static(surface_, "catalog_page");
  lv_obj_set_size(surface_, LV_PCT(100), LV_PCT(100));
  rebuild();
  suppress_refresh_ = true;
  lv_subject_add_observer_obj(view_model_.revision_subject(), revision_changed,
                              surface_, this);
  suppress_refresh_ = false;
  return surface_;
}

void CatalogView::destroy() noexcept {
  lv_async_call_cancel(refresh_async, this);
  refresh_pending_ = false;
  if (surface_)
    lv_obj_delete(surface_);
  surface_ = nullptr;
}

void CatalogView::revision_changed(lv_observer_t *observer, lv_subject_t *) {
  auto *view = static_cast<CatalogView *>(lv_observer_get_user_data(observer));
  if (view && !view->suppress_refresh_)
    view->request_refresh();
}

void CatalogView::refresh_async(void *user_data) {
  auto *view = static_cast<CatalogView *>(user_data);
  view->refresh_pending_ = false;
  if (view->surface_)
    view->rebuild();
}

void CatalogView::request_refresh() noexcept {
  if (!surface_ || refresh_pending_)
    return;
  refresh_pending_ = lv_async_call(refresh_async, this) == LV_RESULT_OK;
}

void CatalogView::rebuild() {
  int scroll_y = 0;
  if (auto *body = lv_obj_find_by_name(surface_, "page_body"))
    scroll_y = lv_obj_get_scroll_y(body);

  lv_obj_clean(surface_);
  build_catalog_view(surface_, view_model_, callbacks_);
  if (callbacks_.layout_changed)
    callbacks_.layout_changed();
  lv_obj_update_layout(surface_);
  if (auto *body = lv_obj_find_by_name(surface_, "page_body"))
    lv_obj_scroll_to_y(body, scroll_y, LV_ANIM_OFF);
}
