#include "pages/installed/installed_view_model.hpp"

InstalledViewModel::InstalledViewModel(StoreViewModel &store) noexcept
    : store_(store) {}

const std::vector<StoreApp> &InstalledViewModel::apps() const noexcept {
  return store_.apps();
}

std::size_t InstalledViewModel::installed_count() const noexcept {
  return store_.model().installed_count();
}

std::size_t InstalledViewModel::update_count() const noexcept {
  return store_.model().update_count();
}

bool InstalledViewModel::select(std::size_t index) noexcept {
  return store_.select(index);
}

lv_subject_t *InstalledViewModel::revision_subject() noexcept {
  return store_.revision_subject();
}
