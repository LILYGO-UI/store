#ifndef LILYGO_UI_STORE_DETAIL_VIEW_MODEL_HPP
#define LILYGO_UI_STORE_DETAIL_VIEW_MODEL_HPP

#include "store_view_model.hpp"

class DetailViewModel {
public:
  explicit DetailViewModel(StoreViewModel &store) noexcept;

  [[nodiscard]] const StoreApp *app() const noexcept;
  [[nodiscard]] std::size_t app_index() const noexcept;
  [[nodiscard]] lv_subject_t *revision_subject() noexcept;

private:
  StoreViewModel &store_;
};

#endif
