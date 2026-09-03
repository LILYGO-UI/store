#include "components/components.hpp"
#include "domain/store_model.hpp"
#include "pages/catalog/catalog_view.hpp"
#include "pages/detail/detail_view.hpp"
#include "pages/installed/installed_view.hpp"

#include <cm0/typography.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr int kMaximumWidth = 1232;
constexpr int kPartialRows = 80;
constexpr char kLongSummary[] =
    "设备文件管理与 Synchronization Console 可同时浏览、复制并安全整理本地与"
    "外部存储中的超长文件名称，保留完整的操作状态与用户输入。";

using FontSet = std::array<const lv_font_t *, 5>;

void flush_display(lv_display_t *display, const lv_area_t *, std::uint8_t *) {
  lv_display_flush_ready(display);
}

void noop_event(lv_event_t *) {}

lv_area_t bounds(lv_obj_t *object) {
  lv_area_t result{};
  lv_obj_get_coords(object, &result);
  return result;
}

void assert_valid_bounds(lv_obj_t *object) {
  const auto area = bounds(object);
  assert(area.x1 <= area.x2);
  assert(area.y1 <= area.y2);
}

void assert_contained(lv_obj_t *parent, lv_obj_t *child) {
  const auto outer = bounds(parent);
  const auto inner = bounds(child);
  if (inner.x1 < outer.x1 || inner.x2 > outer.x2 || inner.y1 < outer.y1 ||
      inner.y2 > outer.y2) {
    char parent_name[64]{};
    char child_name[64]{};
    lv_obj_get_name_resolved(parent, parent_name, sizeof(parent_name));
    lv_obj_get_name_resolved(child, child_name, sizeof(child_name));
    std::fprintf(stderr,
                 "containment failed: %s [%d,%d,%d,%d], %s "
                 "[%d,%d,%d,%d]\n",
                 parent_name, outer.x1, outer.y1, outer.x2, outer.y2,
                 child_name, inner.x1, inner.y1, inner.x2, inner.y2);
  }
  assert(inner.x1 >= outer.x1);
  assert(inner.x2 <= outer.x2);
  assert(inner.y1 >= outer.y1);
  assert(inner.y2 <= outer.y2);
}

void assert_horizontally_contained(lv_obj_t *parent, lv_obj_t *child) {
  const auto outer = bounds(parent);
  const auto inner = bounds(child);
  if (inner.x1 < outer.x1 || inner.x2 > outer.x2) {
    char parent_name[64]{};
    char child_name[64]{};
    lv_obj_get_name_resolved(parent, parent_name, sizeof(parent_name));
    lv_obj_get_name_resolved(child, child_name, sizeof(child_name));
    std::fprintf(
        stderr,
        "horizontal containment failed: %s [%d,%d], %s [%d,%d] "
        "text=%s\n",
        parent_name, outer.x1, outer.x2, child_name, inner.x1, inner.x2,
        lv_obj_check_type(child, &lv_label_class) ? lv_label_get_text(child)
                                                  : "<not-a-label>");
  }
  assert(inner.x1 >= outer.x1);
  assert(inner.x2 <= outer.x2);
}

void assert_horizontal_sequence(std::initializer_list<lv_obj_t *> objects) {
  lv_obj_t *previous = nullptr;
  for (auto *object : objects) {
    assert(object);
    assert_valid_bounds(object);
    if (previous)
      assert(bounds(previous).x2 < bounds(object).x1);
    previous = object;
  }
}

void assert_vertical_sequence(lv_obj_t *parent) {
  lv_obj_t *previous = nullptr;
  const auto count = lv_obj_get_child_count(parent);
  for (std::uint32_t index = 0; index < count; ++index) {
    auto *child = lv_obj_get_child(parent, index);
    assert_valid_bounds(child);
    if (previous)
      assert(bounds(previous).y2 < bounds(child).y1);
    previous = child;
  }
}

std::size_t assert_label_contract(lv_obj_t *object, const FontSet &fonts) {
  std::size_t labels = 0;
  if (lv_obj_check_type(object, &lv_label_class)) {
    const auto *font = lv_obj_get_style_text_font(object, LV_PART_MAIN);
    assert(std::find(fonts.cbegin(), fonts.cend(), font) != fonts.cend());
    assert(lv_obj_get_style_text_letter_space(object, LV_PART_MAIN) == 0);
    assert_horizontally_contained(lv_obj_get_parent(object), object);
    ++labels;
  }

  const auto count = lv_obj_get_child_count(object);
  for (std::uint32_t index = 0; index < count; ++index)
    labels += assert_label_contract(lv_obj_get_child(object, index), fonts);
  return labels;
}

FontSet verify_font_chain() {
  constexpr std::array<int, 5> sizes = {14, 22, 28, 36, 48};
  FontSet fonts{};
  assert(store_fonts_available());

  for (std::size_t index = 0; index < sizes.size(); ++index) {
    fonts[index] = lilygo_ui_font_get(sizes[index]);
    assert(fonts[index]);
    assert(fonts[index] == lilygo_ui_font_get(sizes[index]));
    assert(fonts[index]->fallback);
    assert(fonts[index]->fallback->fallback);

    lv_font_glyph_dsc_t glyph{};
    assert(lv_font_get_glyph_dsc(fonts[index], &glyph, 'A', 0));
    assert(glyph.resolved_font == fonts[index]);
    assert(lv_font_get_glyph_dsc(fonts[index], &glyph, 0x8bbe, 0));
    assert(glyph.resolved_font == fonts[index]->fallback);
    assert(lv_font_get_glyph_dsc(fonts[index], &glyph, 0xf00c, 0));
    assert(glyph.resolved_font == fonts[index]->fallback->fallback);
  }
  return fonts;
}

StoreApp make_long_copy_app() {
  StoreApp app;
  app.id = "long-copy-contract";
  app.app_id = "cc.lilygo.ui.LongCopyContract";
  app.package = "lilygo-ui-long-copy-contract";
  app.name = "设备文件管理与 Synchronization Console Enterprise Preview";
  app.summary = kLongSummary;
  app.description = kLongSummary;
  app.author = "LILYGO 社区维护者与 International Storage Working Group";
  app.version = "2026.09.02-enterprise-preview+arm64.revision.123456789";
  app.latest_version = "2026.10.31-enterprise-preview+arm64.revision.987654321";
  app.size = "123456789.99 MB compressed and verified package";
  app.operation_error =
      "同步失败 "
      "SynchronizationChecksumVerificationTokenWithoutAnyBreaks1234567890 "
      "未能通过完整性校验，请检查存储权限后重试。";
  app.categories = {"Utilities", "设备工具"};
  app.screenshots = {{
      {},
      "主界面 Preview 展示超长中英文文件名称与同步进度，不遮挡后续状态文本。",
      {},
      {},
      {},
      false,
  }};
  app.permissions = {
      {"filesystem-provider-with-an-intentionally-long-identifier",
       "读取、整理并同步 Local and removable storage 中用户明确选择的文件。",
       "read-write", "user-selected"},
      {"background-service",
       "在用户启动同步后继续校验 Integrity checks，并可靠保存操作状态。",
       "service", "application"},
  };
  app.release.version = app.latest_version;
  app.release.status = "published";
  app.available = true;
  app.installed = true;
  app.update_available = true;
  return app;
}

lv_obj_t *make_host(int width, int height) {
  auto *host = lv_obj_create(lv_screen_active());
  lv_obj_set_size(host, width, height);
  lv_obj_set_style_pad_all(host, 0, 0);
  lv_obj_set_style_border_width(host, 0, 0);
  lv_obj_set_style_radius(host, 0, 0);
  lv_obj_clear_flag(host, LV_OBJ_FLAG_SCROLLABLE);
  return host;
}

void assert_catalog_contract(lv_obj_t *host, StoreViewModel &store,
                             const FontSet &fonts) {
  CatalogViewModel view_model(store);
  CatalogView view(view_model,
                   {noop_event, noop_event, noop_event, noop_event, nullptr});
  auto *surface = view.create(host);
  lv_obj_update_layout(surface);

  auto *filter = lv_obj_find_by_name(surface, "source_filter");
  assert(filter && lv_obj_get_child_count(filter) == 3);
  assert_horizontal_sequence({lv_obj_get_child(filter, 0),
                              lv_obj_get_child(filter, 1),
                              lv_obj_get_child(filter, 2)});
  for (std::uint32_t index = 0; index < lv_obj_get_child_count(filter);
       ++index) {
    auto *button = lv_obj_get_child(filter, index);
    assert_contained(filter, button);
    assert(lv_obj_get_child_count(button) == 1);
    assert_contained(button, lv_obj_get_child(button, 0));
  }

  auto *row = lv_obj_find_by_name(surface, "long-copy-contract");
  assert(row && lv_obj_get_child_count(row) == 3);
  auto *icon = lv_obj_get_child(row, 0);
  auto *copy = lv_obj_get_child(row, 1);
  auto *action = lv_obj_get_child(row, 2);
  assert(lv_obj_has_flag(row, LV_OBJ_FLAG_CLICKABLE));
  assert(!lv_obj_has_flag(icon, LV_OBJ_FLAG_CLICKABLE));
  assert(!lv_obj_has_flag(copy, LV_OBJ_FLAG_CLICKABLE));
  assert(lv_obj_has_flag(action, LV_OBJ_FLAG_CLICKABLE));
  assert_horizontal_sequence({icon, copy, action});
  assert_contained(row, icon);
  assert_contained(row, copy);
  assert_contained(row, action);
  assert_vertical_sequence(copy);
  assert(lv_label_get_long_mode(lv_obj_get_child(copy, 0)) ==
         LV_LABEL_LONG_DOT);
  assert(lv_label_get_long_mode(lv_obj_get_child(copy, 1)) ==
         LV_LABEL_LONG_DOT);
  assert_contained(action, lv_obj_get_child(action, 0));
  assert(assert_label_contract(surface, fonts) > 0);

  view.destroy();
  view.destroy();
  assert(lv_obj_get_child_count(host) == 0);
}

void assert_installed_contract(lv_obj_t *host, StoreViewModel &store,
                               const FontSet &fonts) {
  InstalledViewModel view_model(store);
  InstalledView view(view_model, {noop_event, noop_event, noop_event, nullptr});
  auto *surface = view.create(host);
  lv_obj_update_layout(surface);

  auto *summary = lv_obj_find_by_name(surface, "installed_summary");
  assert(summary && lv_obj_get_child_count(summary) == 2);
  assert_horizontal_sequence(
      {lv_obj_get_child(summary, 0), lv_obj_get_child(summary, 1)});

  auto *row = lv_obj_find_by_name(surface, "long-copy-contract");
  assert(row && lv_obj_get_child_count(row) == 3);
  auto *icon = lv_obj_get_child(row, 0);
  auto *copy = lv_obj_get_child(row, 1);
  auto *actions = lv_obj_get_child(row, 2);
  assert(lv_obj_has_flag(row, LV_OBJ_FLAG_CLICKABLE));
  assert(!lv_obj_has_flag(icon, LV_OBJ_FLAG_CLICKABLE));
  assert(!lv_obj_has_flag(copy, LV_OBJ_FLAG_CLICKABLE));
  assert(!lv_obj_has_flag(actions, LV_OBJ_FLAG_CLICKABLE));
  assert_horizontal_sequence({icon, copy, actions});
  assert_contained(row, icon);
  assert_contained(row, copy);
  assert_contained(row, actions);
  assert_vertical_sequence(copy);
  assert(lv_label_get_long_mode(lv_obj_get_child(copy, 0)) ==
         LV_LABEL_LONG_DOT);
  assert(lv_label_get_long_mode(lv_obj_get_child(copy, 1)) ==
         LV_LABEL_LONG_DOT);
  assert(lv_obj_get_child_count(actions) == 2);
  assert_horizontal_sequence(
      {lv_obj_get_child(actions, 0), lv_obj_get_child(actions, 1)});
  assert_contained(actions, lv_obj_get_child(actions, 0));
  assert_contained(actions, lv_obj_get_child(actions, 1));

  auto *storage = lv_obj_find_by_name(surface, "storage_overview");
  assert(storage && lv_obj_get_child_count(storage) == 2);
  assert_horizontal_sequence(
      {lv_obj_get_child(storage, 0), lv_obj_get_child(storage, 1)});
  assert_contained(storage, lv_obj_get_child(storage, 0));
  assert_contained(storage, lv_obj_get_child(storage, 1));
  assert(assert_label_contract(surface, fonts) > 0);

  view.destroy();
  view.destroy();
  assert(lv_obj_get_child_count(host) == 0);
}

void assert_detail_contract(lv_obj_t *host, StoreViewModel &store,
                            const FontSet &fonts) {
  DetailViewModel view_model(store);
  DetailView view(view_model, {noop_event, noop_event, noop_event, nullptr});
  auto *surface = view.create(host);
  lv_obj_update_layout(surface);

  auto *header = lv_obj_find_by_name(surface, "app_header");
  auto *title = lv_obj_find_by_name(surface, "app_title");
  auto *back = lv_obj_find_by_name(surface, "back_button");
  assert(header && title && back);
  assert_horizontal_sequence({back, title});
  assert_contained(header, back);
  assert_contained(header, title);
  assert(lv_label_get_long_mode(title) == LV_LABEL_LONG_DOT);

  auto *body = lv_obj_find_by_name(surface, "page_body");
  assert(body);
  assert_vertical_sequence(body);
  for (std::uint32_t index = 0; index < lv_obj_get_child_count(body); ++index)
    assert_horizontally_contained(body, lv_obj_get_child(body, index));

  auto *identity = lv_obj_find_by_name(surface, "app_identity");
  assert(identity && lv_obj_get_child_count(identity) == 2);
  auto *identity_icon = lv_obj_get_child(identity, 0);
  auto *identity_copy = lv_obj_get_child(identity, 1);
  assert_horizontal_sequence({identity_icon, identity_copy});
  assert_contained(identity, identity_icon);
  assert_contained(identity, identity_copy);
  assert_vertical_sequence(identity_copy);

  auto *app_summary = lv_obj_find_by_name(surface, "app_summary");
  assert(app_summary);
  assert(std::string(lv_label_get_text(app_summary)) == kLongSummary);
  assert(lv_label_get_long_mode(app_summary) == LV_LABEL_LONG_WRAP);

  auto *preview = lv_obj_find_by_name(surface, "preview_section");
  auto *gallery = lv_obj_find_by_name(surface, "screenshot_gallery");
  assert(preview && gallery && lv_obj_get_child_count(gallery) == 1);
  assert_vertical_sequence(preview);
  auto *card = lv_obj_get_child(gallery, 0);
  assert_contained(gallery, card);
  assert(lv_obj_get_child_count(card) == 1);
  auto *fallback = lv_obj_get_child(card, 0);
  assert_vertical_sequence(fallback);

  auto *facts = lv_obj_find_by_name(surface, "version_and_size");
  auto *version = lv_obj_find_by_name(surface, "version_text");
  auto *size = lv_obj_find_by_name(surface, "size_text");
  assert(facts && version && size);
  assert_horizontal_sequence({version, size});
  assert_contained(facts, version);
  assert_contained(facts, size);

  auto *operation_error = lv_obj_find_by_name(surface, "operation_error");
  assert(operation_error && lv_obj_get_child_count(operation_error) == 2);
  auto *operation_icon = lv_obj_get_child(operation_error, 0);
  auto *operation_message = lv_obj_get_child(operation_error, 1);
  assert_horizontal_sequence({operation_icon, operation_message});
  assert_contained(operation_error, operation_icon);
  assert_contained(operation_error, operation_message);
  assert(lv_label_get_long_mode(operation_message) == LV_LABEL_LONG_WRAP);

  auto *primary = lv_obj_find_by_name(surface, "primary_action");
  auto *remove = lv_obj_find_by_name(surface, "remove_action");
  assert(primary && remove);
  assert_contained(primary, lv_obj_get_child(primary, 0));
  assert_contained(remove, lv_obj_get_child(remove, 0));

  auto *notice = lv_obj_find_by_name(surface, "source_notice");
  assert(notice && lv_obj_get_child_count(notice) == 2);
  assert_horizontal_sequence(
      {lv_obj_get_child(notice, 0), lv_obj_get_child(notice, 1)});
  assert_contained(notice, lv_obj_get_child(notice, 0));
  assert_horizontally_contained(notice, lv_obj_get_child(notice, 1));

  auto *permissions = lv_obj_find_by_name(surface, "permission_summary");
  assert(permissions && lv_obj_get_child_count(permissions) == 2);
  assert_vertical_sequence(permissions);
  for (std::uint32_t index = 0; index < lv_obj_get_child_count(permissions);
       ++index) {
    auto *row = lv_obj_get_child(permissions, index);
    assert(lv_obj_get_child_count(row) == 2);
    assert_horizontal_sequence(
        {lv_obj_get_child(row, 0), lv_obj_get_child(row, 1)});
    assert_contained(row, lv_obj_get_child(row, 0));
    assert_contained(row, lv_obj_get_child(row, 1));
  }
  assert(assert_label_contract(surface, fonts) > 0);

  lv_obj_scroll_to_view_recursive(primary, LV_ANIM_OFF);
  lv_obj_update_layout(surface);
  lv_area_t visible_body{};
  lv_obj_get_content_coords(body, &visible_body);
  const auto visible_primary = bounds(primary);
  assert(visible_primary.y1 >= visible_body.y1);
  assert(visible_primary.y2 <= visible_body.y2);
  assert(lv_obj_has_flag(primary, LV_OBJ_FLAG_CLICKABLE));
  lv_obj_send_event(primary, LV_EVENT_CLICKED, nullptr);
  assert(lv_obj_is_valid(primary));

  view.destroy();
  view.destroy();
  assert(lv_obj_get_child_count(host) == 0);
}

void verify_viewport(lv_display_t *display, int width, int height,
                     const FontSet &fonts) {
  lv_obj_clean(lv_screen_active());
  lv_display_set_resolution(display, width, height);
  auto *host = make_host(width, height);
  auto app = make_long_copy_app();
  StoreViewModel store(StoreModel({std::move(app)}));
  assert(store.select(0));

  assert_catalog_contract(host, store, fonts);
  assert_installed_contract(host, store, fonts);
  assert_detail_contract(host, store, fonts);
  lv_obj_delete(host);
}

} // namespace

int main() {
  lv_init();
  std::vector<lv_color32_t> draw_buffer(
      static_cast<std::size_t>(kMaximumWidth) * kPartialRows);
  auto *display = lv_display_create(568, 1232);
  assert(display);
  lv_display_set_buffers(
      display, draw_buffer.data(), nullptr,
      static_cast<std::uint32_t>(draw_buffer.size() * sizeof(lv_color32_t)),
      LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(display, flush_display);
  lv_display_set_default(display);

  const auto fonts = verify_font_chain();
  verify_viewport(display, 320, 568, fonts);
  verify_viewport(display, 568, 1232, fonts);
  verify_viewport(display, 1232, 568, fonts);
  verify_viewport(display, 1024, 768, fonts);

  lv_display_delete(display);
  lilygo_ui_fonts_deinit();
  lv_deinit();
  return 0;
}
