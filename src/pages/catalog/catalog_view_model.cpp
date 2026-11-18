#include "pages/catalog/catalog_view_model.hpp"

CatalogViewModel::CatalogViewModel(StoreViewModel &store, bool runtimes) noexcept : store_(store), runtimes_(runtimes)
{
}

bool CatalogViewModel::runtimes() const noexcept
{
    return runtimes_;
}

std::size_t CatalogViewModel::update_count() const noexcept
{
    std::size_t count = 0;
    for (const auto &app : apps())
        if (is_visible(app) && app.installed && app.update_available) ++count;
    return count;
}

std::size_t CatalogViewModel::uncatalogued_count() const noexcept
{
    std::size_t count = 0;
    for (const auto &app : apps())
        if (is_visible(app) && !app.catalogued) ++count;
    return count;
}

CatalogFilter CatalogViewModel::filter() const noexcept
{
    return store_.model().filter();
}

const std::vector<StoreApp> &CatalogViewModel::apps() const noexcept
{
    return store_.apps();
}

bool CatalogViewModel::is_visible(const StoreApp &app) const noexcept
{
    return runtimes_ ? app.runtime : store_.model().matches_filter(app);
}

RegistryState CatalogViewModel::registry_state() const noexcept
{
    return store_.model().registry_state();
}

const std::string &CatalogViewModel::status_message() const noexcept
{
    return store_.model().status_message();
}

void CatalogViewModel::set_filter(CatalogFilter filter) noexcept
{
    store_.set_filter(filter);
}

bool CatalogViewModel::select(std::size_t index) noexcept
{
    return store_.select(index);
}

lv_subject_t *CatalogViewModel::revision_subject() noexcept
{
    return store_.revision_subject();
}
