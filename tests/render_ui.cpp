#include "app.hpp"
#include "app_identity.h"
#include "domain/store_model.hpp"
#include "pages/catalog/catalog_view.hpp"
#include "pages/detail/detail_view.hpp"
#include "pages/installed/installed_view.hpp"
#include "registry/sha256.hpp"

#include <cm0/status_bar.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

constexpr int kPartialRows = 80;
constexpr int kHomeIndicatorHeight = 38;
constexpr std::uint32_t kMaximumVisibleScreenshots = 9;
constexpr std::uint32_t kPage = 0xf2f2f7;
constexpr std::uint32_t kSurface = 0xffffff;

std::vector<lv_color32_t> framebuffer;
int display_width = 0;

void flush_display(lv_display_t *display, const lv_area_t *area,
                   std::uint8_t *pixels) {
  const int width = area->x2 - area->x1 + 1;
  const auto *source = reinterpret_cast<const lv_color32_t *>(pixels);
  for (int y = area->y1; y <= area->y2; ++y) {
    const auto source_offset = static_cast<std::size_t>(y - area->y1) * width;
    const auto destination_offset =
        static_cast<std::size_t>(y) * display_width +
        static_cast<std::size_t>(area->x1);
    std::copy_n(source + source_offset, width,
                framebuffer.begin() + destination_offset);
  }
  lv_display_flush_ready(display);
}

bool write_ppm(const char *path, int width, int height) {
  std::ofstream file(path, std::ios::binary);
  if (!file)
    return false;
  file << "P6\n" << width << ' ' << height << "\n255\n";
  for (const auto &pixel : framebuffer) {
    file.put(static_cast<char>(pixel.red));
    file.put(static_cast<char>(pixel.green));
    file.put(static_cast<char>(pixel.blue));
  }
  return file.good();
}

std::string stage_path(const char *path, const char *stage) {
  std::string result(path);
  const auto extension = result.rfind(".ppm");
  result.insert(extension == std::string::npos ? result.size() : extension,
                stage);
  return result;
}

std::string file_url(const std::filesystem::path &path) {
  return "file://" + std::filesystem::absolute(path).string();
}

void write_text(const std::filesystem::path &path, const std::string &value) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  output << value;
  assert(output.good());
}

struct RegistryFixturePaths {
  std::filesystem::path icon;
  std::filesystem::path root;
};

RegistryFixturePaths prepare_registry_fixture(const char *output_path,
                                              int width, int height) {
  const auto root = std::filesystem::path(output_path).parent_path() /
                    ("registry-fixture-" + std::to_string(width) + "x" +
                     std::to_string(height));
  constexpr const char *snapshot = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
  const auto snapshot_root = root / "v1" / "snapshots" / snapshot;
  const auto detail_path = snapshot_root / "apps" / "cc.lilygo.ui.Demo.json";
  const auto index_path = snapshot_root / "index.json";
  const auto package_path = root / "demo.deb";
  write_text(package_path, "payload");
  const auto asset_root =
      std::filesystem::path(__FILE__).parent_path().parent_path() / "assets";
  const auto preview_path = asset_root / "previews" / "store-portrait.png";
  const auto icon_source_path = asset_root / "app-icon.png";
  const auto icon_path = root / "assets" / "demo-icon.png";
  assert(std::filesystem::is_regular_file(preview_path));
  assert(std::filesystem::is_regular_file(icon_source_path));
  std::filesystem::create_directories(icon_path.parent_path());
  std::error_code copy_error;
  std::filesystem::copy_file(icon_source_path, icon_path,
                             std::filesystem::copy_options::overwrite_existing,
                             copy_error);
  assert(!copy_error && std::filesystem::is_regular_file(icon_path));

  std::vector<std::string> screenshot_urls;
  screenshot_urls.reserve(10);
  std::string screenshots = "[";
  for (int index = 0; index < 10; ++index) {
    const auto screenshot_path =
        root / "assets" / ("preview-" + std::to_string(index + 1) + ".png");
    copy_error.clear();
    std::filesystem::copy_file(
        preview_path, screenshot_path,
        std::filesystem::copy_options::overwrite_existing, copy_error);
    assert(!copy_error && std::filesystem::is_regular_file(screenshot_path));
    const auto screenshot_url = file_url(screenshot_path);
    assert(std::find(screenshot_urls.cbegin(), screenshot_urls.cend(),
                     screenshot_url) == screenshot_urls.cend());
    screenshot_urls.push_back(screenshot_url);
    if (index > 0)
      screenshots += ',';
    screenshots += "{\"url\":\"" + screenshot_url +
                   "\",\"caption\":\"Preview " + std::to_string(index + 1) +
                   "\"}";
  }
  screenshots += ']';

  const auto icon_url = file_url(icon_path);
  assert(std::find(screenshot_urls.cbegin(), screenshot_urls.cend(),
                   icon_url) == screenshot_urls.cend());
  const auto detail_url = file_url(detail_path);
  const auto detail =
      std::string("{") +
      "\"protocol_version\":1,"
      "\"app_id\":\"cc.lilygo.ui.Demo\","
      "\"package\":\"lilygo-ui-demo\","
      "\"title\":\"File Manager\","
      "\"summary\":\"Browse, copy, and manage files on your device.\","
      "\"description\":\"Browse, copy, and manage files on your device.\","
      "\"authors\":[{\"name\":\"LILYGO\",\"github\":\"LILYGO\"}],"
      "\"categories\":[\"Utilities\"],"
      "\"license\":\"MIT\","
      "\"source_repo\":\"https://github.com/LILYGO-UI/file-manager\","
      "\"homepage\":\"file:///tmp/home\","
      "\"icon_url\":\"" +
      icon_url +
      "\","
      "\"screenshots\":" +
      screenshots +
      ","
      "\"permissions\":[{\"id\":\"filesystem\","
      "\"reason\":\"Read and manage local files\"}],"
      "\"releases\":[{"
      "\"version\":\"1.3.0\","
      "\"architecture\":\"arm64\","
      "\"status\":\"published\","
      "\"size\":5033165,"
      "\"sha256\":"
      "\"239f59ed55e737c77147cf55ad0c1b030b6d7ee748a7426952f9b852d5a935e5\","
      "\"url\":\"" +
      file_url(package_path) +
      "\","
      "\"min_appkit_version\":\"0.1.0\","
      "\"published_at\":\"2026-08-31T12:00:00+08:00\"}]}";
  write_text(detail_path, detail);

  const auto index =
      std::string("{") +
      "\"protocol_version\":1,"
      "\"snapshot_id\":\"" +
      snapshot +
      "\",\"generated_at\":\"2026-08-31T12:00:00+08:00\","
      "\"apps\":[{"
      "\"app_id\":\"cc.lilygo.ui.Demo\","
      "\"package\":\"lilygo-ui-demo\","
      "\"title\":\"File Manager\","
      "\"summary\":\"Browse, copy, and manage files on your device.\","
      "\"categories\":[\"Utilities\"],"
      "\"icon_url\":\"" +
      icon_url +
      "\",\"latest_version\":\"1.3.0\","
      "\"detail_url\":\"" +
      detail_url + "\"}]}";
  write_text(index_path, index);

  const auto root_document =
      std::string("{") +
      "\"protocol_version\":1,"
      "\"snapshot_id\":\"" +
      snapshot +
      "\",\"generated_at\":\"2026-08-31T12:00:00+08:00\","
      "\"index\":{\"url\":\"" +
      file_url(index_path) + "\",\"sha256\":\"" + sha256(index) + "\"}}";
  const auto root_path = root / "v1" / "root.json";
  write_text(root_path, root_document);
  setenv("LILYGO_UI_STORE_REGISTRY_URL", file_url(root_path).c_str(), 1);
  setenv("LILYGO_UI_STORE_ARCHITECTURE", "arm64", 1);
  setenv("LILYGO_UI_APPKIT_VERSION", "0.1.0", 1);
  const auto cache_nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const auto cache_home = root / ("cache-" + std::to_string(getpid()) + "-" +
                                  std::to_string(cache_nonce));
  assert(!std::filesystem::exists(cache_home));
  std::filesystem::create_directories(cache_home);
  setenv("XDG_CACHE_HOME", cache_home.string().c_str(), 1);
  return {icon_path, root_path};
}

void write_stage(lv_display_t *display, const char *path, const char *stage,
                 int width, int height) {
  lv_obj_invalidate(lv_screen_active());
  lv_refr_now(display);
  const auto output = stage_path(path, stage);
  assert(write_ppm(output.c_str(), width, height));
}

std::string button_text(lv_obj_t *button) {
  assert(button && lv_obj_get_child_count(button) > 0);
  return lv_label_get_text(lv_obj_get_child(button, 0));
}

bool app_icon_has_image(lv_obj_t *root, const std::string &app_id) {
  const auto name = "app_icon:" + app_id;
  auto *icon = lv_obj_find_by_name(root, name.c_str());
  return icon && lv_obj_get_child_count(icon) == 1 &&
         lv_obj_check_type(lv_obj_get_child(icon, 0), &lv_image_class) &&
         lv_image_get_src(lv_obj_get_child(icon, 0)) != nullptr;
}

void assert_card_top_has_rendered_pixels(lv_obj_t *card, int display_height) {
  lv_area_t bounds{};
  lv_obj_get_coords(card, &bounds);
  const int left = std::max(0, bounds.x1 + 16);
  const int right = std::min(display_width - 1, bounds.x2 - 16);
  const int top = std::max(0, bounds.y1 + 12);
  const int bottom = std::min(display_height - 1, bounds.y1 + 56);
  assert(left <= right && top <= bottom);

  const auto &first =
      framebuffer[static_cast<std::size_t>(top) * display_width + left];
  bool has_non_white = false;
  bool has_variation = false;
  for (int y = top; y <= bottom; ++y) {
    for (int x = left; x <= right; ++x) {
      const auto &pixel =
          framebuffer[static_cast<std::size_t>(y) * display_width + x];
      has_non_white = has_non_white || pixel.red != 255 || pixel.green != 255 ||
                      pixel.blue != 255;
      has_variation = has_variation || pixel.red != first.red ||
                      pixel.green != first.green || pixel.blue != first.blue;
    }
  }
  assert(has_non_white);
  assert(has_variation);
}

void noop_event(lv_event_t *) {}

lv_obj_t *create_appkit_root() {
  auto *screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_hex(kPage), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

  auto *status = cm0_status_bar_create(lv_layer_top());
  assert(status);
  lv_obj_align(status, LV_ALIGN_TOP_MID, 0, 0);

  auto *content = lv_obj_create(screen);
  lv_obj_set_name_static(content, "appkit_content_root");
  lv_obj_set_size(content, LV_PCT(100), LV_PCT(100));
  lv_obj_align(content, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_pad_all(content, 0, 0);
  lv_obj_set_style_pad_top(content, CM0_STATUS_BAR_HEIGHT, 0);
  lv_obj_set_style_border_width(content, 0, 0);
  lv_obj_set_style_radius(content, 0, 0);
  lv_obj_set_style_bg_color(content, lv_color_hex(kPage), 0);
  lv_obj_set_style_bg_opa(content, LV_OPA_COVER, 0);
  lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

  auto *home = cm0_home_indicator_create(lv_layer_top());
  assert(home);
  lv_obj_set_size(home, LV_PCT(100), kHomeIndicatorHeight);
  lv_obj_align(home, LV_ALIGN_BOTTOM_MID, 0, -3);
  return content;
}

void assert_responsive_shell_layout(int width, int height) {
  auto *surface = lv_obj_find_by_name(lv_screen_active(), "app_surface");
  auto *content = lv_obj_find_by_name(lv_screen_active(), "app_content");
  auto *navigation = lv_obj_find_by_name(lv_screen_active(), "app_navigation");
  assert(surface && content && navigation);

  lv_area_t surface_bounds{};
  lv_area_t content_bounds{};
  lv_area_t navigation_bounds{};
  lv_obj_get_coords(surface, &surface_bounds);
  lv_obj_get_coords(content, &content_bounds);
  lv_obj_get_coords(navigation, &navigation_bounds);
  assert(surface_bounds.y1 == CM0_STATUS_BAR_HEIGHT);
  assert(lv_area_get_height(&surface_bounds) == height - CM0_STATUS_BAR_HEIGHT);
  assert(navigation_bounds.y2 <= height - kHomeIndicatorHeight + 2);

  if (width >= 720) {
    assert(content_bounds.x1 == navigation_bounds.x2 + 1);
    assert(lv_obj_get_width(navigation) == 180);
    assert(lv_obj_get_height(navigation) ==
           height - CM0_STATUS_BAR_HEIGHT - kHomeIndicatorHeight);
  } else {
    assert(navigation_bounds.y1 == content_bounds.y2 + 1);
    assert(lv_obj_get_height(navigation) == 64);
    assert(lv_obj_get_width(navigation) == width);
  }
}

void resize_display(lv_display_t *display, int width, int height) {
  display_width = width;
  framebuffer.assign(static_cast<std::size_t>(width) * height, {});
  lv_display_set_resolution(display, width, height);
  lv_tick_inc(1);
  lv_timer_handler();
  lv_obj_update_layout(lv_screen_active());
  assert(lv_display_get_horizontal_resolution(display) == width);
  assert(lv_display_get_vertical_resolution(display) == height);
}

void assert_initial_layout(int width, int height) {
  auto *surface = lv_obj_find_by_name(lv_screen_active(), "app_surface");
  auto *content = lv_obj_find_by_name(lv_screen_active(), "app_content");
  auto *navigation = lv_obj_find_by_name(lv_screen_active(), "app_navigation");
  auto *header = lv_obj_find_by_name(lv_screen_active(), "app_header");
  auto *body = lv_obj_find_by_name(lv_screen_active(), "page_body");
  auto *filter = lv_obj_find_by_name(lv_screen_active(), "source_filter");
  auto *title = lv_obj_find_by_name(lv_screen_active(), "app_title");
  assert(surface && content && navigation && header && body && filter && title);

  lv_area_t surface_bounds{};
  lv_area_t content_bounds{};
  lv_area_t navigation_bounds{};
  lv_area_t header_bounds{};
  lv_area_t body_bounds{};
  lv_obj_get_coords(surface, &surface_bounds);
  lv_obj_get_coords(content, &content_bounds);
  lv_obj_get_coords(navigation, &navigation_bounds);
  lv_obj_get_coords(header, &header_bounds);
  lv_obj_get_coords(body, &body_bounds);
  assert(surface_bounds.y1 == CM0_STATUS_BAR_HEIGHT);
  assert(lv_area_get_height(&surface_bounds) == height - CM0_STATUS_BAR_HEIGHT);
  assert(navigation_bounds.y2 <= height - kHomeIndicatorHeight + 2);
  assert(lv_obj_get_height(header) == 82);
  assert(body_bounds.y1 == header_bounds.y2 + 1);
  assert(lv_obj_get_width(body) <= 720);
  assert(lv_color_eq(lv_obj_get_style_bg_color(surface, LV_PART_MAIN),
                     lv_color_hex(kSurface)));
  assert(lv_color_eq(lv_obj_get_style_bg_color(navigation, LV_PART_MAIN),
                     lv_color_hex(kSurface)));
  assert(lv_obj_get_child_count(navigation) == 2);
  assert(std::string(lv_label_get_text(title)) == LILYGO_UI_STORE_APP_NAME);
  assert(lv_obj_get_child_count(filter) == 3);
  assert(button_text(lv_obj_get_child(filter, 0)) == "All");
  assert(button_text(lv_obj_get_child(filter, 1)) == "Official");
  assert(button_text(lv_obj_get_child(filter, 2)) == "Community");
  assert(std::string(lv_label_get_text(
             lv_obj_get_child(lv_obj_get_child(navigation, 0), 1))) == "Store");
  assert(std::string(lv_label_get_text(lv_obj_get_child(
             lv_obj_get_child(navigation, 1), 1))) == "Installed");

  assert_responsive_shell_layout(width, height);

  if (width >= 720) {
    assert(content_bounds.x1 == navigation_bounds.x2 + 1);
    assert(lv_obj_get_width(navigation) == 180);
    assert(lv_obj_get_height(navigation) ==
           height - CM0_STATUS_BAR_HEIGHT - kHomeIndicatorHeight);
  } else {
    assert(navigation_bounds.y1 == content_bounds.y2 + 1);
    assert(lv_obj_get_height(navigation) == 64);
    assert(lv_obj_get_width(navigation) == width);
  }
}

void assert_detail_resize_round_trip(lv_display_t *display,
                                     int expected_body_scroll_y) {
  auto *screen = lv_screen_active();
  auto *surface = lv_obj_find_by_name(screen, "app_surface");
  auto *page = lv_obj_find_by_name(screen, "detail_page");
  auto *body = lv_obj_find_by_name(screen, "page_body");
  auto *gallery = lv_obj_find_by_name(screen, "screenshot_gallery");
  auto *title = lv_obj_find_by_name(screen, "app_title");
  auto *primary = lv_obj_find_by_name(screen, "primary_action");
  assert(surface && page && body && gallery && title && primary);
  assert(std::string(lv_label_get_text(title)) == "File Manager");
  assert(expected_body_scroll_y > 0);
  assert(lv_obj_get_scroll_y(body) == expected_body_scroll_y);
  assert(lv_obj_get_scroll_x(gallery) == 80);

  auto *focus_group = lv_group_create();
  assert(focus_group);
  lv_group_add_obj(focus_group, primary);
  lv_group_focus_obj(primary);
  assert(lv_group_get_focused(focus_group) == primary);

  resize_display(display, 1232, 568);
  assert_responsive_shell_layout(1232, 568);
  assert(lv_obj_find_by_name(screen, "app_surface") == surface);
  assert(lv_obj_find_by_name(screen, "detail_page") == page);
  assert(lv_obj_find_by_name(screen, "page_body") == body);
  assert(lv_obj_find_by_name(screen, "screenshot_gallery") == gallery);
  assert(lv_obj_find_by_name(screen, "primary_action") == primary);
  assert(lv_obj_get_scroll_y(body) == expected_body_scroll_y);
  assert(lv_obj_get_scroll_x(gallery) == 80);
  assert(lv_group_get_focused(focus_group) == primary);
  assert(lv_obj_find_by_name(screen, "back_button"));
  assert(std::string(lv_label_get_text(
             lv_obj_find_by_name(screen, "app_title"))) == "File Manager");

  resize_display(display, 568, 1232);
  assert_responsive_shell_layout(568, 1232);
  assert(lv_obj_find_by_name(screen, "app_surface") == surface);
  assert(lv_obj_find_by_name(screen, "detail_page") == page);
  assert(lv_obj_find_by_name(screen, "page_body") == body);
  assert(lv_obj_find_by_name(screen, "screenshot_gallery") == gallery);
  assert(lv_obj_find_by_name(screen, "primary_action") == primary);
  assert(lv_obj_get_scroll_y(body) == expected_body_scroll_y);
  assert(lv_obj_get_scroll_x(gallery) == 80);
  assert(lv_group_get_focused(focus_group) == primary);

  lv_group_delete(focus_group);
  assert(lv_obj_get_group(primary) == nullptr);
}

void assert_app_closed(lv_obj_t *root) {
  assert(root);
  assert(!lv_obj_find_by_name(root, "app_surface"));
  assert(!lv_obj_find_by_name(root, "catalog_page"));
  assert(!lv_obj_find_by_name(root, "installed_page"));
  assert(!lv_obj_find_by_name(root, "detail_page"));
}

void exercise_app_lifecycle(const cm0_app_descriptor_t *descriptor,
                            const cm0_app_context_t &context) {
  auto *installed_nav =
      lv_obj_find_by_name(context.root, "navigation_installed");
  assert(installed_nav);
  assert(!lv_obj_find_by_name(context.root, "installed_page"));

  lv_obj_send_event(installed_nav, LV_EVENT_CLICKED, nullptr);
  assert(lv_obj_is_valid(installed_nav));
  assert(!lv_obj_find_by_name(context.root, "installed_page"));
  descriptor->close();
  assert_app_closed(context.root);

  lv_tick_inc(1);
  lv_timer_handler();
  lv_obj_update_layout(context.root);
  assert_app_closed(context.root);

  descriptor->close();
  lv_tick_inc(1);
  lv_timer_handler();
  assert_app_closed(context.root);

  for (int iteration = 0; iteration < 2; ++iteration) {
    descriptor->open(&context);
    lv_obj_update_layout(context.root);
    auto *surface = lv_obj_find_by_name(context.root, "app_surface");
    assert(surface && lv_obj_get_parent(surface) == context.root);
    assert(lv_obj_find_by_name(surface, "catalog_page"));

    descriptor->close();
    assert_app_closed(context.root);
    lv_tick_inc(1);
    lv_timer_handler();
    assert_app_closed(context.root);
  }
}

void exercise_cached_catalog_fallback(const cm0_app_descriptor_t *descriptor,
                                      const cm0_app_context_t &context,
                                      const std::filesystem::path &root_path) {
  descriptor->close();
  assert_app_closed(context.root);

  std::error_code error;
  assert(std::filesystem::remove(root_path, error));
  assert(!error);

  descriptor->open(&context);
  lv_obj_t *demo = nullptr;
  lv_obj_t *retry = nullptr;
  for (int attempt = 0; attempt < 400 && (!demo || !retry); ++attempt) {
    lv_tick_inc(10);
    lv_timer_handler();
    lv_obj_update_layout(context.root);
    demo = lv_obj_find_by_name(context.root, "demo");
    retry = lv_obj_find_by_name(context.root, "registry_retry");
    if (!demo || !retry)
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  assert(demo);
  assert(retry);
}

StoreApp make_probe_app(const char *id, bool installed, bool update,
                        const std::string &icon_path) {
  StoreApp app;
  app.id = id;
  app.app_id = "cc.lilygo.ui." + std::string(id);
  app.package = "lilygo-ui-" + std::string(id);
  app.name = id;
  app.summary = "Action state probe";
  app.version = update ? "1.0.0" : "1.2.3";
  app.latest_version = "1.2.3";
  app.size = "1.0 MB";
  app.icon_local_path = icon_path;
  app.available = true;
  app.installed = installed;
  app.update_available = update;
  app.categories = {"Utilities"};
  app.release.version = app.latest_version;
  app.release.status = "published";
  return app;
}

void assert_action_labels(const std::string &icon_path) {
  const auto get = make_probe_app("probe-get", false, false, icon_path);
  const auto installed =
      make_probe_app("probe-installed", true, false, icon_path);
  const auto update = make_probe_app("probe-update", true, true, icon_path);

  auto *probe = lv_obj_create(lv_layer_top());
  lv_obj_set_size(probe, 320, 900);
  lv_obj_set_style_pad_all(probe, 0, 0);
  lv_obj_set_flex_flow(probe, LV_FLEX_FLOW_COLUMN);

  StoreViewModel catalog_store(StoreModel({get, installed, update}));
  CatalogViewModel catalog(catalog_store);
  CatalogView catalog_view(
      catalog, {noop_event, noop_event, noop_event, noop_event, nullptr});
  catalog_view.create(probe);
  lv_obj_update_layout(probe);

  auto *get_row = lv_obj_find_by_name(probe, "probe-get");
  auto *installed_row = lv_obj_find_by_name(probe, "probe-installed");
  auto *update_row = lv_obj_find_by_name(probe, "probe-update");
  assert(get_row && installed_row && update_row);
  assert(app_icon_has_image(get_row, get.app_id));
  assert(app_icon_has_image(installed_row, installed.app_id));
  assert(app_icon_has_image(update_row, update.app_id));
  assert(button_text(lv_obj_get_child(get_row, 2)) == "Get");
  assert(button_text(lv_obj_get_child(installed_row, 2)) == "Installed");
  assert(button_text(lv_obj_get_child(update_row, 2)) == "Update");

  catalog_view.destroy();
  catalog_view.destroy();
  assert(lv_obj_get_child_count(probe) == 0);
  StoreViewModel installed_store(StoreModel({get, installed, update}));
  InstalledViewModel installed_view_model(installed_store);
  InstalledView installed_view(installed_view_model,
                               {noop_event, noop_event, noop_event, nullptr});
  installed_view.create(probe);
  lv_obj_update_layout(probe);

  installed_row = lv_obj_find_by_name(probe, "probe-installed");
  update_row = lv_obj_find_by_name(probe, "probe-update");
  assert(installed_row && update_row);
  assert(app_icon_has_image(installed_row, installed.app_id));
  assert(app_icon_has_image(update_row, update.app_id));
  assert(lv_obj_get_height(installed_row) == 96);
  assert(lv_obj_get_height(update_row) == 96);
  auto *open_action =
      lv_obj_find_by_name(installed_row, "installed_primary_action");
  auto *update_action =
      lv_obj_find_by_name(update_row, "installed_primary_action");
  auto *action_group =
      lv_obj_find_by_name(installed_row, "installed_action_group");
  assert(open_action && update_action);
  assert(action_group && lv_obj_get_width(action_group) == 116);
  assert(lv_obj_get_height(action_group) == 44);
  assert(lv_obj_get_child_count(action_group) == 2);
  assert(lv_obj_get_width(open_action) == 64);
  assert(lv_obj_get_width(lv_obj_get_child(action_group, 1)) == 44);
  assert(lv_obj_get_style_pad_column(installed_row, LV_PART_MAIN) == 12);
  assert(lv_obj_get_style_pad_column(action_group, LV_PART_MAIN) == 8);
  assert(button_text(open_action) == "Open");
  assert(button_text(update_action) == "Update");
  installed_view.destroy();
  installed_view.destroy();
  assert(lv_obj_get_child_count(probe) == 0);

  StoreViewModel detail_store(StoreModel({get, installed, update}));
  assert(detail_store.select(0));
  DetailViewModel detail_view_model(detail_store);
  DetailView detail_view(detail_view_model,
                         {noop_event, noop_event, noop_event, nullptr});
  detail_view.create(probe);
  lv_obj_update_layout(probe);
  assert(lv_obj_get_child_count(probe) == 1);
  assert(lv_obj_find_by_name(probe, "detail_page"));
  assert(lv_obj_find_by_name(probe, "primary_action"));
  detail_view.destroy();
  detail_view.destroy();
  assert(lv_obj_get_child_count(probe) == 0);
  lv_obj_delete(probe);
}

void exercise_store_workflow(lv_display_t *display, const char *output_path,
                             int width, int height) {
  lv_obj_t *demo = nullptr;
  for (int attempt = 0; attempt < 250 && !demo; ++attempt) {
    lv_tick_inc(10);
    lv_timer_handler();
    lv_obj_update_layout(lv_screen_active());
    demo = lv_obj_find_by_name(lv_screen_active(), "demo");
    if (!demo)
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  if (!demo) {
    auto *status = lv_obj_find_by_name(lv_screen_active(), "registry_status");
    if (status && lv_obj_get_child_count(status) > 0) {
      auto *label = lv_obj_get_child(status, 0);
      std::fprintf(stderr, "Registry status: %s\n", lv_label_get_text(label));
    }
  }
  assert(demo);
  bool catalog_icon_loaded = false;
  for (int attempt = 0; attempt < 250 && !catalog_icon_loaded; ++attempt) {
    lv_tick_inc(10);
    lv_timer_handler();
    lv_obj_update_layout(lv_screen_active());
    demo = lv_obj_find_by_name(lv_screen_active(), "demo");
    catalog_icon_loaded = demo && app_icon_has_image(demo, "cc.lilygo.ui.Demo");
    if (!catalog_icon_loaded)
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  assert(catalog_icon_loaded);
  write_stage(display, output_path, "", width, height);

  lv_obj_send_event(demo, LV_EVENT_CLICKED, nullptr);
  assert(lv_obj_is_valid(demo));
  lv_obj_t *back = nullptr;
  for (int attempt = 0; attempt < 10 && !back; ++attempt) {
    lv_tick_inc(1);
    lv_timer_handler();
    lv_obj_update_layout(lv_screen_active());
    back = lv_obj_find_by_name(lv_screen_active(), "back_button");
  }
  auto *identity = lv_obj_find_by_name(lv_screen_active(), "app_identity");
  auto *gallery = lv_obj_find_by_name(lv_screen_active(), "screenshot_gallery");
  auto *primary = lv_obj_find_by_name(lv_screen_active(), "primary_action");
  auto *body = lv_obj_find_by_name(lv_screen_active(), "page_body");
  auto *summary = lv_obj_find_by_name(lv_screen_active(), "app_summary");
  auto *preview = lv_obj_find_by_name(lv_screen_active(), "preview_section");
  auto *notice = lv_obj_find_by_name(lv_screen_active(), "source_notice");
  assert(back && identity && gallery && primary && body && summary && preview &&
         notice);
  assert(app_icon_has_image(identity, "cc.lilygo.ui.Demo"));
  assert(lv_obj_get_width(back) == 48);
  assert(lv_obj_get_height(back) == 48);
  assert(lv_obj_get_height(identity) == 96);
  assert(lv_obj_get_height(summary) >= 32);
  assert(lv_obj_get_style_pad_top(body, LV_PART_MAIN) == 20);
  assert(lv_obj_get_style_pad_left(body, LV_PART_MAIN) == 16);
  assert(lv_obj_get_style_pad_row(body, LV_PART_MAIN) == 16);
  assert(lv_obj_get_style_pad_row(preview, LV_PART_MAIN) == 10);
  assert(lv_obj_get_height(notice) == 72);
  assert(lv_obj_get_child_count(gallery) == kMaximumVisibleScreenshots);
  lv_obj_scroll_to_x(gallery, 80, LV_ANIM_OFF);
  lv_obj_update_layout(lv_screen_active());
  assert(lv_obj_get_scroll_x(gallery) == 80);
  if (width == 568 && height == 1232) {
    for (std::uint32_t index = 0; index < lv_obj_get_child_count(gallery);
         ++index) {
      auto *card = lv_obj_get_child(gallery, index);
      assert(lv_obj_get_width(card) == 166);
      assert(lv_obj_get_height(card) == 360);
    }
  }
  bool screenshots_loaded = false;
  for (int attempt = 0; attempt < 250 && !screenshots_loaded; ++attempt) {
    lv_tick_inc(10);
    lv_timer_handler();
    lv_obj_update_layout(lv_screen_active());
    gallery = lv_obj_find_by_name(lv_screen_active(), "screenshot_gallery");
    screenshots_loaded = gallery && lv_obj_get_child_count(gallery) ==
                                        kMaximumVisibleScreenshots;
    for (std::uint32_t index = 0;
         screenshots_loaded && index < lv_obj_get_child_count(gallery);
         ++index) {
      auto *card = lv_obj_get_child(gallery, index);
      screenshots_loaded =
          lv_obj_get_child_count(card) == 1 &&
          lv_obj_check_type(lv_obj_get_child(card, 0), &lv_image_class) &&
          lv_image_get_src(lv_obj_get_child(card, 0)) != nullptr;
    }
    if (!screenshots_loaded)
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  assert(screenshots_loaded);
  assert(lv_obj_get_scroll_x(gallery) == 80);
  if (width == 568 && height == 1232) {
    auto *second_card = lv_obj_get_child(gallery, 1);
    lv_obj_invalidate(second_card);
    lv_refr_now(display);
    assert_card_top_has_rendered_pixels(second_card, height);

    body = lv_obj_find_by_name(lv_screen_active(), "page_body");
    assert(body);
    auto *scroll_probe = lv_obj_create(body);
    lv_obj_set_name_static(scroll_probe, "detail_scroll_probe");
    lv_obj_set_size(scroll_probe, LV_PCT(100), 320);
    lv_obj_set_style_min_height(scroll_probe, 320, 0);
    lv_obj_set_style_pad_all(scroll_probe, 0, 0);
    lv_obj_set_style_border_width(scroll_probe, 0, 0);
    lv_obj_set_style_bg_opa(scroll_probe, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(scroll_probe, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_update_layout(lv_screen_active());
    lv_obj_scroll_to_y(body, 120, LV_ANIM_OFF);
    lv_obj_update_layout(lv_screen_active());
    const int body_scroll_y = lv_obj_get_scroll_y(body);
    assert(body_scroll_y > 0);

    assert_detail_resize_round_trip(display, body_scroll_y);
    lv_obj_delete(scroll_probe);
    lv_obj_update_layout(lv_screen_active());
    lv_obj_scroll_to_y(body, 0, LV_ANIM_OFF);
  }
  lv_obj_scroll_to_x(gallery, 0, LV_ANIM_OFF);
  primary = lv_obj_find_by_name(lv_screen_active(), "primary_action");
  assert(button_text(primary) == "Get App");
  assert(lv_obj_find_by_name(lv_screen_active(), "permission_summary"));
  assert(!lv_obj_find_by_name(lv_screen_active(), "remove_action"));
  write_stage(display, output_path, "-detail", width, height);

  auto *navigation = lv_obj_find_by_name(lv_screen_active(), "app_navigation");
  auto *installed_nav = lv_obj_get_child(navigation, 1);
  lv_obj_send_event(installed_nav, LV_EVENT_CLICKED, nullptr);
  assert(lv_obj_is_valid(installed_nav));
  lv_obj_t *installed_summary = nullptr;
  for (int attempt = 0; attempt < 10 && !installed_summary; ++attempt) {
    lv_tick_inc(1);
    lv_timer_handler();
    lv_obj_update_layout(lv_screen_active());
    installed_summary =
        lv_obj_find_by_name(lv_screen_active(), "installed_summary");
  }
  assert(!lv_obj_find_by_name(lv_screen_active(), "source_filter"));
  assert(installed_summary);
  assert(lv_obj_find_by_name(lv_screen_active(), "storage_overview"));
  assert(lv_obj_find_by_name(lv_screen_active(), "installed_empty_state"));
  write_stage(display, output_path, "-installed", width, height);

  navigation = lv_obj_find_by_name(lv_screen_active(), "app_navigation");
  auto *catalog_nav = lv_obj_get_child(navigation, 0);
  lv_obj_send_event(catalog_nav, LV_EVENT_CLICKED, nullptr);
  assert(lv_obj_is_valid(catalog_nav));
  demo = nullptr;
  for (int attempt = 0; attempt < 10 && !demo; ++attempt) {
    lv_tick_inc(1);
    lv_timer_handler();
    lv_obj_update_layout(lv_screen_active());
    demo = lv_obj_find_by_name(lv_screen_active(), "demo");
  }
  assert(demo && lv_obj_get_child_count(demo) == 3);
  auto *action = lv_obj_get_child(demo, 2);
  assert(button_text(action) == "Get");
}

} // namespace

int main(int argc, char **argv) {
  assert(argc == 4);
  const int width = std::atoi(argv[1]);
  const int height = std::atoi(argv[2]);
  assert(width >= 320 && height >= 240);
  const auto fixture = prepare_registry_fixture(argv[3], width, height);

  display_width = width;
  framebuffer.resize(static_cast<std::size_t>(width) * height);
  std::vector<lv_color32_t> draw_buffer(static_cast<std::size_t>(width) *
                                        kPartialRows);
  lv_init();
  auto *display = lv_display_create(width, height);
  assert(display);
  lv_display_set_buffers(
      display, draw_buffer.data(), nullptr,
      static_cast<std::uint32_t>(draw_buffer.size() * sizeof(lv_color32_t)),
      LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(display, flush_display);
  lv_display_set_default(display);

  cm0_app_context_t context{};
  context.root = create_appkit_root();
  const auto *descriptor = cm0_app_get_descriptor();
  descriptor->open(&context);
  lv_obj_update_layout(lv_screen_active());
  assert_initial_layout(width, height);

  lv_tick_inc(20);
  lv_refr_now(display);
  const bool has_contrast = std::any_of(
      framebuffer.cbegin() + 1, framebuffer.cend(), [&](const auto &pixel) {
        const auto &first = framebuffer.front();
        return pixel.red != first.red || pixel.green != first.green ||
               pixel.blue != first.blue;
      });
  assert(has_contrast);
  assert(write_ppm(argv[3], width, height));

  exercise_store_workflow(display, argv[3], width, height);
  assert_action_labels(fixture.icon.string());

  if (width == 568 && height == 1232) {
    exercise_cached_catalog_fallback(descriptor, context, fixture.root);
    exercise_app_lifecycle(descriptor, context);
  } else {
    descriptor->close();
  }
  lv_display_delete(display);
  lilygo_ui_fonts_deinit();
  lv_deinit();
  return 0;
}
