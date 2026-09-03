#ifndef LILYGO_UI_STORE_STORE_VIEW_MODEL_HPP
#define LILYGO_UI_STORE_STORE_VIEW_MODEL_HPP

#include "components/lv_subject.hpp"
#include "domain/store_model.hpp"

#include <optional>
#include <string>
#include <vector>

struct StoreInstallCommand {
  std::string package;
  std::string app_id;
  std::string app_name;
  RegistryRelease release;
  bool was_update = false;
};

struct StoreRemoveCommand {
  std::string package;
  std::string app_id;
};

struct StoreIconDownloadCommand {
  std::string app_id;
  std::string url;
  std::string cache_key;
};

struct StoreScreenshotDownloadCommand {
  std::string app_id;
  std::size_t index = 0;
  std::string url;
  std::string cache_key;
};

class StoreViewModel {
public:
  StoreViewModel();
  explicit StoreViewModel(StoreModel model);

  [[nodiscard]] const StoreModel &model() const noexcept;
  [[nodiscard]] const std::vector<StoreApp> &apps() const noexcept;
  [[nodiscard]] const StoreApp *app(std::size_t index) const noexcept;
  [[nodiscard]] std::size_t find_app(const std::string &app_id) const noexcept;
  [[nodiscard]] lv_subject_t *revision_subject() noexcept;

  void set_filter(CatalogFilter filter) noexcept;
  bool select(std::size_t index) noexcept;
  void begin_loading(std::string message = "Updating the app catalog...");
  void replace_catalog(std::vector<StoreApp> apps, std::string message = {});
  void set_registry_error(std::string error);

  [[nodiscard]] std::optional<StoreInstallCommand>
  begin_install(std::size_t index, bool operation_running,
                bool refresh_running);
  [[nodiscard]] std::optional<StoreRemoveCommand>
  begin_remove(std::size_t index, bool operation_running, bool refresh_running);
  void fail_operation(const std::string &app_id, std::string error);
  void complete_install(const std::string &app_id, bool was_update,
                        std::string installed_version);
  void complete_remove(const std::string &app_id);

  [[nodiscard]] std::vector<StoreIconDownloadCommand> begin_icon_downloads();
  [[nodiscard]] std::vector<StoreScreenshotDownloadCommand>
  begin_screenshot_downloads(std::size_t app_index);
  bool complete_icon(const std::string &app_id, const std::string &url,
                     const std::string &cache_key, std::string local_path,
                     std::string error);
  bool complete_screenshot(const std::string &app_id, std::size_t image_index,
                           const std::string &url, const std::string &cache_key,
                           std::string local_path, std::string error);

private:
  void publish() noexcept;

  StoreModel model_;
  std::int32_t revision_ = 0;
  IntSubject revision_subject_{0};
};

#endif
