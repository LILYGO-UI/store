#ifndef LILYGO_UI_STORE_CATALOG_VIEW_HPP
#define LILYGO_UI_STORE_CATALOG_VIEW_HPP

#include "pages/catalog/catalog_view_model.hpp"

#include <lvgl.h>

struct CatalogViewCallbacks {
  lv_event_cb_t select_app;
  lv_event_cb_t quick_action;
  lv_event_cb_t set_filter;
  lv_event_cb_t refresh;
  void (*layout_changed)();
};

class CatalogView {
public:
  CatalogView(CatalogViewModel &view_model,
              CatalogViewCallbacks callbacks) noexcept;

  lv_obj_t *create(lv_obj_t *parent);
  void destroy() noexcept;

private:
  static void revision_changed(lv_observer_t *observer, lv_subject_t *subject);
  static void refresh_async(void *user_data);

  void request_refresh() noexcept;
  void rebuild();

  CatalogViewModel &view_model_;
  CatalogViewCallbacks callbacks_;
  lv_obj_t *surface_ = nullptr;
  bool suppress_refresh_ = false;
  bool refresh_pending_ = false;
};

#endif
