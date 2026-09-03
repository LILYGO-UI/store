#include "pages/detail/detail_view_model.hpp"

DetailViewModel::DetailViewModel(StoreViewModel &store) noexcept
    : store_(store) {}

const StoreApp *DetailViewModel::app() const noexcept {
  return store_.model().selected_app();
}

std::size_t DetailViewModel::app_index() const noexcept {
  return store_.model().selected_index();
}

lv_subject_t *DetailViewModel::revision_subject() noexcept {
  return store_.revision_subject();
}
