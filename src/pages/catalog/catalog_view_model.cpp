#include "pages/catalog/catalog_view_model.hpp"

CatalogViewModel::CatalogViewModel(StoreViewModel &store) noexcept
    : store_(store) {}

CatalogFilter CatalogViewModel::filter() const noexcept {
  return store_.model().filter();
}

const std::vector<StoreApp> &CatalogViewModel::apps() const noexcept {
  return store_.apps();
}

bool CatalogViewModel::is_visible(const StoreApp &app) const noexcept {
  return store_.model().matches_filter(app);
}

RegistryState CatalogViewModel::registry_state() const noexcept {
  return store_.model().registry_state();
}

const std::string &CatalogViewModel::status_message() const noexcept {
  return store_.model().status_message();
}

void CatalogViewModel::set_filter(CatalogFilter filter) noexcept {
  store_.set_filter(filter);
}

bool CatalogViewModel::select(std::size_t index) noexcept {
  return store_.select(index);
}

lv_subject_t *CatalogViewModel::revision_subject() noexcept {
  return store_.revision_subject();
}
