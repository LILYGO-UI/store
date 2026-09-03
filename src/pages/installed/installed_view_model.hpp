#ifndef LILYGO_UI_STORE_INSTALLED_VIEW_MODEL_HPP
#define LILYGO_UI_STORE_INSTALLED_VIEW_MODEL_HPP

#include "store_view_model.hpp"

class InstalledViewModel {
public:
  explicit InstalledViewModel(StoreViewModel &store) noexcept;

  [[nodiscard]] const std::vector<StoreApp> &apps() const noexcept;
  [[nodiscard]] std::size_t installed_count() const noexcept;
  [[nodiscard]] std::size_t update_count() const noexcept;
  bool select(std::size_t index) noexcept;
  [[nodiscard]] lv_subject_t *revision_subject() noexcept;

private:
  StoreViewModel &store_;
};

#endif
