#ifndef LILYGO_UI_STORE_COMPONENTS_HPP
#define LILYGO_UI_STORE_COMPONENTS_HPP

#include "domain/store_model.hpp"

#include <lvgl.h>

#include <cstddef>
#include <cstdint>

inline constexpr std::uint32_t COLOR_PAGE = 0xf2f2f7;
inline constexpr std::uint32_t COLOR_SURFACE = 0xffffff;
inline constexpr std::uint32_t COLOR_TITLE = 0x1a1a1a;
inline constexpr std::uint32_t COLOR_TEXT = 0x000000;
inline constexpr std::uint32_t COLOR_SECONDARY = 0x6e6e73;
inline constexpr std::uint32_t COLOR_SEPARATOR = 0xd8dde3;
inline constexpr std::uint32_t COLOR_ACTION = 0x20262d;
inline constexpr std::uint32_t COLOR_ACCENT = 0x0878d1;
inline constexpr std::uint32_t COLOR_ACCENT_SOFT = 0xe5f2fc;
inline constexpr std::uint32_t COLOR_SUCCESS = 0x218739;
inline constexpr std::uint32_t COLOR_SUCCESS_SOFT = 0xe4f4e8;
inline constexpr std::uint32_t COLOR_WARNING = 0xe97916;
inline constexpr std::uint32_t COLOR_WARNING_SOFT = 0xffeedf;
inline constexpr std::uint32_t COLOR_DANGER = 0xc9342f;
inline constexpr std::uint32_t COLOR_DANGER_SOFT = 0xffe8e7;
inline constexpr std::uint32_t COLOR_DANGER_BORDER = 0xf0b7b4;
inline constexpr std::uint32_t COLOR_CONTROL = 0xe3e4e8;
inline constexpr std::uint32_t COLOR_DISABLED_SURFACE = 0xeeeeef;
inline constexpr std::uint32_t COLOR_DISABLED_TEXT = 0x8e8e93;

void style_box(lv_obj_t *object, std::uint32_t color, int radius = 0);
[[nodiscard]] bool store_fonts_available() noexcept;
lv_obj_t *make_label(lv_obj_t *parent, const char *text, int font_size,
                     std::uint32_t color);
lv_obj_t *make_flex(lv_obj_t *parent, lv_flex_flow_t flow);
lv_obj_t *make_button(lv_obj_t *parent, const char *text, int font_size,
                      std::uint32_t background, std::uint32_t foreground,
                      lv_event_cb_t callback = nullptr,
                      void *user_data = nullptr);
lv_obj_t *make_app_icon(lv_obj_t *parent, const StoreApp &app,
                        std::size_t index, int size);
void update_app_icon(lv_obj_t *root, const StoreApp &app, std::size_t index);
lv_obj_t *make_header(lv_obj_t *parent, const char *title,
                      lv_event_cb_t back_callback = nullptr,
                      const char *action = nullptr);
lv_obj_t *make_page_body(lv_obj_t *parent);
lv_obj_t *make_card(lv_obj_t *parent, int height = LV_SIZE_CONTENT);

#endif
