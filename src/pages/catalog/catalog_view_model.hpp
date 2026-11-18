#ifndef LILYGO_UI_STORE_CATALOG_VIEW_MODEL_HPP
#define LILYGO_UI_STORE_CATALOG_VIEW_MODEL_HPP

#include "store_view_model.hpp"

class CatalogViewModel {
public:
    explicit CatalogViewModel(StoreViewModel &store, bool runtimes = false) noexcept;

    [[nodiscard]] bool runtimes() const noexcept;
    [[nodiscard]] std::size_t update_count() const noexcept;
    [[nodiscard]] std::size_t uncatalogued_count() const noexcept;

    [[nodiscard]] CatalogFilter filter() const noexcept;
    [[nodiscard]] const std::vector<StoreApp> &apps() const noexcept;
    [[nodiscard]] bool is_visible(const StoreApp &app) const noexcept;
    [[nodiscard]] RegistryState registry_state() const noexcept;
    [[nodiscard]] const std::string &status_message() const noexcept;

    void set_filter(CatalogFilter filter) noexcept;
    bool select(std::size_t index) noexcept;
    [[nodiscard]] lv_subject_t *revision_subject() noexcept;

private:
    StoreViewModel &store_;
    bool runtimes_ = false;
};

#endif
