#include "components/components.hpp"

#include <cm0/typography.h>

#include <string>

namespace {

const lv_font_t *store_font(int font_size) noexcept {
  switch (font_size) {
  case 14:
  case 22:
  case 28:
  case 36:
  case 48:
    return lilygo_ui_font_get(font_size);
  default:
    LV_LOG_ERROR("Unsupported Store font size: %d", font_size);
    return nullptr;
  }
}

} // namespace

void style_box(lv_obj_t *object, std::uint32_t color, int radius) {
  lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_grad_dir(object, LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_border_width(object, 0, 0);
  lv_obj_set_style_radius(object, radius, 0);
  lv_obj_set_style_shadow_width(object, 0, 0);
}

bool store_fonts_available() noexcept {
  static constexpr int sizes[] = {14, 22, 28, 36, 48};
  for (const auto size : sizes) {
    if (!store_font(size))
      return false;
  }
  return true;
}

lv_obj_t *make_label(lv_obj_t *parent, const char *text, int font_size,
                     std::uint32_t color) {
  auto *label = lv_label_create(parent);
  lv_label_set_text(label, text);
  const auto *font = store_font(font_size);
  if (!font) {
    LV_LOG_ERROR("Unable to load Store font at %d px", font_size);
    lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    return label;
  }
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
  lv_obj_set_style_text_letter_space(label, 0, 0);
  return label;
}

lv_obj_t *make_flex(lv_obj_t *parent, lv_flex_flow_t flow) {
  auto *container = lv_obj_create(parent);
  style_box(container, COLOR_PAGE);
  lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(container, 0, 0);
  lv_obj_set_style_pad_row(container, 0, 0);
  lv_obj_set_style_pad_column(container, 0, 0);
  lv_obj_clear_flag(container, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(container, flow);
  return container;
}

lv_obj_t *make_button(lv_obj_t *parent, const char *text, int font_size,
                      std::uint32_t background, std::uint32_t foreground,
                      lv_event_cb_t callback, void *user_data) {
  auto *button = lv_button_create(parent);
  style_box(button, background, 8);
  lv_obj_set_height(button, 44);
  lv_obj_set_style_pad_hor(button, 12, 0);
  lv_obj_set_style_pad_ver(button, 0, 0);
  auto *label = make_label(button, text, font_size, foreground);
  lv_obj_center(label);
  if (callback)
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user_data);
  else
    lv_obj_clear_flag(button, LV_OBJ_FLAG_CLICKABLE);
  return button;
}

namespace {

const char *app_symbol(std::size_t index) {
  static constexpr const char *symbols[] = {
      LV_SYMBOL_TINT, LV_SYMBOL_DIRECTORY, LV_SYMBOL_WIFI,
      LV_SYMBOL_FILE, LV_SYMBOL_AUDIO,     LV_SYMBOL_SETTINGS,
  };
  return index < 6 ? symbols[index] : LV_SYMBOL_DOWNLOAD;
}

std::uint32_t app_color(std::size_t index) {
  static constexpr std::uint32_t colors[] = {
      0x1688d4, 0xe97916, 0x218739, 0x7b5bc7, 0xd34e70, 0x59636e,
  };
  return index < 6 ? colors[index] : COLOR_ACCENT;
}

std::uint32_t app_soft_color(std::size_t index) {
  static constexpr std::uint32_t colors[] = {
      0xe3f3fd, 0xffeedf, 0xe4f4e8, 0xeee8fb, 0xfbe6ec, 0xe9edf0,
  };
  return index < 6 ? colors[index] : COLOR_ACCENT_SOFT;
}

std::string app_icon_name(const StoreApp &app) {
  return "app_icon:" + app.app_id;
}

void populate_app_icon(lv_obj_t *icon, const StoreApp &app, std::size_t index,
                       int size) {
  lv_obj_clean(icon);
  if (!app.icon_local_path.empty()) {
    auto *image = lv_image_create(icon);
    lv_obj_set_size(image, LV_PCT(100), LV_PCT(100));
    lv_image_set_inner_align(image, LV_IMAGE_ALIGN_COVER);
    lv_image_set_src(image, app.icon_local_path.c_str());
    return;
  }
  auto *glyph = make_label(icon, app_symbol(index), size >= 64 ? 36 : 22,
                           app_color(index));
  lv_obj_center(glyph);
}

} // namespace

lv_obj_t *make_app_icon(lv_obj_t *parent, const StoreApp &app,
                        std::size_t index, int size) {
  auto *icon = lv_obj_create(parent);
  const auto name = app_icon_name(app);
  lv_obj_set_name(icon, name.c_str());
  style_box(icon, app_soft_color(index), size >= 88 ? 12 : 8);
  lv_obj_set_size(icon, size, size);
  lv_obj_set_style_pad_all(icon, 0, 0);
  lv_obj_set_style_clip_corner(icon, true, 0);
  lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(icon, LV_OBJ_FLAG_SCROLLABLE);
  populate_app_icon(icon, app, index, size);
  return icon;
}

void update_app_icon(lv_obj_t *root, const StoreApp &app, std::size_t index) {
  if (!root)
    return;
  const auto name = app_icon_name(app);
  auto *icon = lv_obj_find_by_name(root, name.c_str());
  if (!icon)
    return;
  const auto size = lv_obj_get_width(icon);
  populate_app_icon(icon, app, index, size > 0 ? size : 56);
}

lv_obj_t *make_header(lv_obj_t *parent, const char *title,
                      lv_event_cb_t back_callback, const char *action) {
  auto *header = make_flex(parent, LV_FLEX_FLOW_ROW);
  lv_obj_set_name_static(header, "app_header");
  lv_obj_set_size(header, LV_PCT(100), 82);
  style_box(header, COLOR_SURFACE);
  lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_color(header, lv_color_hex(COLOR_SEPARATOR), 0);
  lv_obj_set_style_border_width(header, 1, 0);
  lv_obj_set_style_pad_hor(header, 16, 0);
  lv_obj_set_style_pad_column(header, 12, 0);
  lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  if (back_callback) {
    auto *back = make_button(header, LV_SYMBOL_LEFT, 22, COLOR_SURFACE,
                             COLOR_ACCENT, back_callback, nullptr);
    lv_obj_set_name_static(back, "back_button");
    lv_obj_set_size(back, 48, 48);
    lv_obj_set_style_pad_all(back, 0, 0);
    lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
  }

  auto *heading = make_label(header, title, 36, COLOR_TITLE);
  lv_obj_set_name_static(heading, "app_title");
  lv_obj_set_width(heading, 0);
  lv_obj_set_height(heading, 48);
  lv_obj_set_flex_grow(heading, 1);
  lv_label_set_long_mode(heading, LV_LABEL_LONG_DOT);

  if (action) {
    auto *action_label = make_label(header, action, 22, COLOR_ACCENT);
    lv_obj_set_name_static(action_label, "header_action");
  }
  return header;
}

lv_obj_t *make_page_body(lv_obj_t *parent) {
  auto *body = lv_obj_create(parent);
  lv_obj_set_name_static(body, "page_body");
  style_box(body, COLOR_PAGE);
  lv_obj_set_width(body, LV_PCT(100));
  lv_obj_set_flex_grow(body, 1);
  lv_obj_set_style_pad_all(body, 16, 0);
  lv_obj_set_style_pad_row(body, 12, 0);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(body, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_AUTO);
  return body;
}

lv_obj_t *make_card(lv_obj_t *parent, int height) {
  auto *card = lv_obj_create(parent);
  style_box(card, COLOR_SURFACE, 8);
  lv_obj_set_width(card, LV_PCT(100));
  lv_obj_set_height(card, height);
  lv_obj_set_style_pad_all(card, 16, 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  return card;
}
