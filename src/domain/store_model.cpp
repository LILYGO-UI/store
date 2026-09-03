#include "domain/store_model.hpp"

#include <algorithm>
#include <utility>

StoreModel::StoreModel(std::vector<StoreApp> apps)
    : apps_(std::move(apps)), registry_state_(RegistryState::ready),
      status_message_() {}

CatalogFilter StoreModel::filter() const noexcept { return filter_; }

const std::vector<StoreApp> &StoreModel::apps() const noexcept { return apps_; }

const StoreApp *StoreModel::app(std::size_t index) const noexcept {
  return index < apps_.size() ? &apps_[index] : nullptr;
}

StoreApp *StoreModel::app(std::size_t index) noexcept {
  return index < apps_.size() ? &apps_[index] : nullptr;
}

const StoreApp *StoreModel::selected_app() const noexcept {
  const auto found =
      std::find_if(apps_.cbegin(), apps_.cend(), [&](const auto &item) {
        return item.app_id == selected_app_id_;
      });
  return found == apps_.cend() ? nullptr : &*found;
}

std::size_t StoreModel::selected_index() const noexcept {
  const auto found =
      std::find_if(apps_.cbegin(), apps_.cend(), [&](const auto &item) {
        return item.app_id == selected_app_id_;
      });
  return found == apps_.cend()
             ? apps_.size()
             : static_cast<std::size_t>(std::distance(apps_.cbegin(), found));
}

std::size_t StoreModel::installed_count() const noexcept {
  return static_cast<std::size_t>(
      std::count_if(apps_.cbegin(), apps_.cend(),
                    [](const auto &item) { return item.installed; }));
}

std::size_t StoreModel::update_count() const noexcept {
  return static_cast<std::size_t>(
      std::count_if(apps_.cbegin(), apps_.cend(), [](const auto &item) {
        return item.installed && item.update_available;
      }));
}

bool StoreModel::matches_filter(const StoreApp &item) const noexcept {
  if (filter_ == CatalogFilter::official)
    return item.official;
  if (filter_ == CatalogFilter::community)
    return !item.official;
  return true;
}

RegistryState StoreModel::registry_state() const noexcept {
  return registry_state_;
}

const std::string &StoreModel::status_message() const noexcept {
  return status_message_;
}

void StoreModel::set_filter(CatalogFilter filter) noexcept { filter_ = filter; }

bool StoreModel::select(std::size_t index) noexcept {
  if (!app(index))
    return false;
  selected_app_id_ = apps_[index].app_id;
  return true;
}

void StoreModel::begin_loading(std::string message) {
  registry_state_ = RegistryState::loading;
  status_message_ = std::move(message);
}

void StoreModel::replace_catalog(std::vector<StoreApp> apps,
                                 std::string message) {
  apps_ = std::move(apps);
  registry_state_ = RegistryState::ready;
  status_message_ = std::move(message);
  if (!selected_app_id_.empty() && !selected_app())
    selected_app_id_.clear();
}

void StoreModel::set_registry_error(std::string error) {
  registry_state_ = RegistryState::error;
  status_message_ = std::move(error);
}

void StoreModel::set_status_message(std::string message) {
  status_message_ = std::move(message);
}

bool StoreModel::set_busy(std::size_t index, bool busy) noexcept {
  auto *item = app(index);
  if (!item)
    return false;
  item->busy = busy;
  return true;
}

bool StoreModel::install(std::size_t index, std::string version) {
  auto *item = app(index);
  if (!item || item->installed)
    return false;
  item->installed = true;
  item->update_available = false;
  item->version = version.empty() ? item->latest_version : std::move(version);
  item->operation_error.clear();
  item->busy = false;
  return true;
}

bool StoreModel::update(std::size_t index, std::string version) {
  auto *item = app(index);
  if (!item || !item->installed || !item->update_available)
    return false;
  item->version = version.empty() ? item->latest_version : std::move(version);
  item->operation_error.clear();
  item->update_available = false;
  item->busy = false;
  return true;
}

bool StoreModel::remove(std::size_t index) noexcept {
  auto *item = app(index);
  if (!item || !item->installed)
    return false;
  item->installed = false;
  item->operation_error.clear();
  item->update_available = false;
  item->version = item->latest_version;
  item->busy = false;
  return true;
}
