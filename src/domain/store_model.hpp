#ifndef LILYGO_UI_STORE_DOMAIN_STORE_MODEL_HPP
#define LILYGO_UI_STORE_DOMAIN_STORE_MODEL_HPP

#include "registry/registry_client.hpp"

#include <cstddef>
#include <string>
#include <vector>

enum class CatalogFilter { all, official, community };
enum class RegistryState { loading, ready, error };

[[nodiscard]] bool is_launcher_package(const std::string &package) noexcept;
[[nodiscard]] bool is_removable_package(const std::string &package) noexcept;

struct StoreScreenshot {
    std::string url;
    std::string caption;
    std::string cache_key;
    std::string local_path;
    std::string load_error;
    bool loading = false;
};

struct StoreApp {
    std::string id;
    std::string app_id;
    std::string package;
    std::string name;
    std::string summary;
    std::string description;
    std::string author;
    std::string version;
    std::string latest_version;
    std::string size;
    std::string icon_url;
    std::string icon_cache_key;
    std::string icon_local_path;
    std::string icon_load_error;
    std::string operation_error;
    std::vector<std::string> categories;
    std::vector<StoreScreenshot> screenshots;
    std::vector<RegistryPermission> permissions;
    RegistryRelease release;
    bool official         = false;
    bool icon_loading     = false;
    bool installed        = false;
    bool update_available = false;
    bool available        = true;
    bool busy             = false;
};

class StoreModel {
public:
    StoreModel() = default;
    explicit StoreModel(std::vector<StoreApp> apps);

    [[nodiscard]] CatalogFilter filter() const noexcept;
    [[nodiscard]] const std::vector<StoreApp> &apps() const noexcept;
    [[nodiscard]] const StoreApp *app(std::size_t index) const noexcept;
    [[nodiscard]] StoreApp *app(std::size_t index) noexcept;
    [[nodiscard]] const StoreApp *selected_app() const noexcept;
    [[nodiscard]] std::size_t selected_index() const noexcept;
    [[nodiscard]] std::size_t installed_count() const noexcept;
    [[nodiscard]] std::size_t update_count() const noexcept;
    [[nodiscard]] bool matches_filter(const StoreApp &app) const noexcept;
    [[nodiscard]] RegistryState registry_state() const noexcept;
    [[nodiscard]] const std::string &status_message() const noexcept;

    void set_filter(CatalogFilter filter) noexcept;
    bool select(std::size_t index) noexcept;
    void begin_loading(std::string message = "Updating the app catalog...");
    void replace_catalog(std::vector<StoreApp> apps, std::string message = {});
    void set_registry_error(std::string error);
    void set_status_message(std::string message);
    bool set_busy(std::size_t index, bool busy) noexcept;
    bool install(std::size_t index, std::string version = {});
    bool update(std::size_t index, std::string version = {});
    bool remove(std::size_t index) noexcept;

private:
    std::vector<StoreApp> apps_;
    CatalogFilter filter_ = CatalogFilter::all;
    std::string selected_app_id_;
    RegistryState registry_state_ = RegistryState::loading;
    std::string status_message_   = "Loading the app catalog...";
};

#endif
