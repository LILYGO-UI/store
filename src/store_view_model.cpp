#include "store_view_model.hpp"

#include <algorithm>
#include <limits>
#include <utility>

StoreViewModel::StoreViewModel() = default;

StoreViewModel::StoreViewModel(StoreModel model) : model_(std::move(model)) {}

const StoreModel &StoreViewModel::model() const noexcept { return model_; }

const std::vector<StoreApp> &StoreViewModel::apps() const noexcept {
  return model_.apps();
}

const StoreApp *StoreViewModel::app(std::size_t index) const noexcept {
  return model_.app(index);
}

std::size_t StoreViewModel::find_app(const std::string &app_id) const noexcept {
  for (std::size_t index = 0; index < model_.apps().size(); ++index) {
    if (model_.apps()[index].app_id == app_id)
      return index;
  }
  return model_.apps().size();
}

lv_subject_t *StoreViewModel::revision_subject() noexcept {
  return revision_subject_.get();
}

void StoreViewModel::set_filter(CatalogFilter filter) noexcept {
  model_.set_filter(filter);
  publish();
}

bool StoreViewModel::select(std::size_t index) noexcept {
  if (!model_.select(index))
    return false;
  publish();
  return true;
}

void StoreViewModel::begin_loading(std::string message) {
  model_.begin_loading(std::move(message));
  publish();
}

void StoreViewModel::replace_catalog(std::vector<StoreApp> apps,
                                     std::string message) {
  model_.replace_catalog(std::move(apps), std::move(message));
  publish();
}

void StoreViewModel::set_registry_error(std::string error) {
  model_.set_registry_error(std::move(error));
  publish();
}

std::optional<StoreInstallCommand>
StoreViewModel::begin_install(std::size_t index, bool operation_running,
                              bool refresh_running) {
  auto *item = model_.app(index);
  if (!item || operation_running || refresh_running || item->busy ||
      !item->available || (item->installed && !item->update_available))
    return std::nullopt;

  item->busy = true;
  item->operation_error.clear();
  model_.set_status_message("Downloading and verifying " + item->name + "...");
  StoreInstallCommand command{item->package, item->app_id, item->name,
                              item->release, item->installed};
  publish();
  return command;
}

std::optional<StoreRemoveCommand>
StoreViewModel::begin_remove(std::size_t index, bool operation_running,
                             bool refresh_running) {
  auto *item = model_.app(index);
  if (!item || operation_running || refresh_running || item->busy ||
      !item->installed)
    return std::nullopt;

  item->busy = true;
  item->operation_error.clear();
  model_.set_status_message("Removing " + item->name + "...");
  StoreRemoveCommand command{item->package, item->app_id};
  publish();
  return command;
}

void StoreViewModel::fail_operation(const std::string &app_id,
                                    std::string error) {
  auto *item = model_.app(find_app(app_id));
  if (!item)
    return;
  item->busy = false;
  item->operation_error = error;
  model_.set_status_message(std::move(error));
  publish();
}

void StoreViewModel::complete_install(const std::string &app_id,
                                      bool was_update,
                                      std::string installed_version) {
  const auto index = find_app(app_id);
  auto *item = model_.app(index);
  if (!item)
    return;
  const auto name = item->name;
  item->busy = false;
  if (was_update)
    model_.update(index, std::move(installed_version));
  else
    model_.install(index, std::move(installed_version));
  model_.set_status_message((was_update ? "Updated " : "Installed ") + name);
  publish();
}

void StoreViewModel::complete_remove(const std::string &app_id) {
  const auto index = find_app(app_id);
  auto *item = model_.app(index);
  if (!item)
    return;
  const auto name = item->name;
  item->busy = false;
  model_.remove(index);
  model_.set_status_message("Removed " + name);
  publish();
}

std::vector<StoreIconDownloadCommand> StoreViewModel::begin_icon_downloads() {
  std::vector<StoreIconDownloadCommand> pending;
  pending.reserve(model_.apps().size());
  for (std::size_t index = 0; index < model_.apps().size(); ++index) {
    auto *item = model_.app(index);
    if (!item || item->icon_url.empty() || item->icon_cache_key.empty() ||
        item->icon_loading || !item->icon_local_path.empty() ||
        !item->icon_load_error.empty())
      continue;
    item->icon_loading = true;
    pending.push_back({item->app_id, item->icon_url, item->icon_cache_key});
  }
  if (!pending.empty())
    publish();
  return pending;
}

std::vector<StoreScreenshotDownloadCommand>
StoreViewModel::begin_screenshot_downloads(std::size_t app_index) {
  auto *item = model_.app(app_index);
  if (!item)
    return {};

  std::vector<StoreScreenshotDownloadCommand> pending;
  const auto count = std::min<std::size_t>(item->screenshots.size(), 9);
  pending.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    auto &screenshot = item->screenshots[index];
    if (screenshot.loading || !screenshot.local_path.empty() ||
        !screenshot.load_error.empty())
      continue;
    screenshot.loading = true;
    pending.push_back(
        {item->app_id, index, screenshot.url, screenshot.cache_key});
  }
  if (!pending.empty())
    publish();
  return pending;
}

bool StoreViewModel::complete_icon(const std::string &app_id,
                                   const std::string &url,
                                   const std::string &cache_key,
                                   std::string local_path, std::string error) {
  auto *item = model_.app(find_app(app_id));
  if (!item || item->icon_url != url || item->icon_cache_key != cache_key)
    return false;
  item->icon_loading = false;
  item->icon_local_path = std::move(local_path);
  item->icon_load_error = std::move(error);
  publish();
  return true;
}

bool StoreViewModel::complete_screenshot(
    const std::string &app_id, std::size_t image_index, const std::string &url,
    const std::string &cache_key, std::string local_path, std::string error) {
  auto *item = model_.app(find_app(app_id));
  if (!item || image_index >= item->screenshots.size())
    return false;
  auto &screenshot = item->screenshots[image_index];
  if (screenshot.url != url || screenshot.cache_key != cache_key)
    return false;
  screenshot.loading = false;
  screenshot.local_path = std::move(local_path);
  screenshot.load_error = std::move(error);
  publish();
  return true;
}

void StoreViewModel::publish() noexcept {
  if (revision_ == std::numeric_limits<std::int32_t>::max())
    revision_ = 0;
  else
    ++revision_;
  revision_subject_.set(revision_);
}
