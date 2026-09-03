#ifndef LILYGO_UI_STORE_DETAIL_VIEW_HPP
#define LILYGO_UI_STORE_DETAIL_VIEW_HPP

#include "pages/detail/detail_view_model.hpp"

#include <lvgl.h>

struct DetailViewCallbacks {
  lv_event_cb_t back;
  lv_event_cb_t primary_action;
  lv_event_cb_t remove;
  void (*layout_changed)();
};

class DetailView {
public:
  DetailView(DetailViewModel &view_model,
             DetailViewCallbacks callbacks) noexcept;

  lv_obj_t *create(lv_obj_t *parent);
  void destroy() noexcept;

private:
  static void revision_changed(lv_observer_t *observer, lv_subject_t *subject);
  static void refresh_async(void *user_data);

  void request_refresh() noexcept;
  void rebuild();

  DetailViewModel &view_model_;
  DetailViewCallbacks callbacks_;
  lv_obj_t *surface_ = nullptr;
  bool suppress_refresh_ = false;
  bool refresh_pending_ = false;
};

#endif
