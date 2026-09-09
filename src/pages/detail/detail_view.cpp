#include "pages/detail/detail_view.hpp"

#include "components/components.hpp"

#include <cm0/typography.h>

#include <algorithm>
#include <cstdio>

namespace {

void create_identity(lv_obj_t *body, const StoreApp &app, std::size_t index)
{
    auto *identity_row = make_flex(body, LV_FLEX_FLOW_ROW);
    lv_obj_set_name_static(identity_row, "app_identity");
    lv_obj_set_size(identity_row, LV_PCT(100), 96);
    lv_obj_set_style_pad_column(identity_row, 16, 0);
    lv_obj_set_flex_align(identity_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    make_app_icon(identity_row, app, index, 88);
    auto *copy = make_flex(identity_row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(copy, 0);
    lv_obj_set_height(copy, 72);
    lv_obj_set_flex_grow(copy, 1);
    lv_obj_set_style_pad_row(copy, 4, 0);

    auto *name = make_label(copy, app.name.c_str(), 28, COLOR_TITLE);
    lv_obj_set_width(name, LV_PCT(100));
    lv_obj_set_height(name, 38);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    const char *publisher = app.author.empty() ? "Unknown publisher" : app.author.c_str();
    if (app.official) publisher = "LILYGO Official";
    auto *publisher_label = make_label(copy, publisher, 14, app.official ? COLOR_ACCENT : COLOR_SECONDARY);
    lv_obj_set_width(publisher_label, LV_PCT(100));
    lv_obj_set_height(publisher_label, 22);
    lv_label_set_long_mode(publisher_label, LV_LABEL_LONG_DOT);
}

void populate_screenshot_card(lv_obj_t *card, const StoreScreenshot &screenshot)
{
    lv_obj_clean(card);
    if (!screenshot.local_path.empty()) {
        auto *image = lv_image_create(card);
        lv_obj_set_size(image, LV_PCT(100), LV_PCT(100));
        lv_image_set_inner_align(image, LV_IMAGE_ALIGN_COVER);
        lv_image_set_src(image, screenshot.local_path.c_str());
        return;
    }

    auto *fallback = make_flex(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_size(fallback, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(fallback, 12, 0);
    lv_obj_set_style_pad_row(fallback, 8, 0);
    lv_obj_set_flex_align(fallback, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    make_label(fallback, LV_SYMBOL_FILE, 28, screenshot.load_error.empty() ? COLOR_ACCENT : COLOR_SECONDARY);
    auto *caption = make_label(fallback, screenshot.caption.c_str(), 14, COLOR_TEXT);
    lv_obj_set_width(caption, LV_PCT(100));
    lv_obj_set_height(caption, 180);
    lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(caption, LV_LABEL_LONG_DOT);
    make_label(fallback, screenshot.load_error.empty() ? "Loading..." : "Unavailable", 14, COLOR_SECONDARY);
}

void create_preview(lv_obj_t *body, const StoreApp &app)
{
    auto *section = make_flex(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_name_static(section, "preview_section");
    lv_obj_set_size(section, LV_PCT(100), 400);
    lv_obj_set_style_pad_row(section, 10, 0);

    auto *heading = make_flex(section, LV_FLEX_FLOW_ROW);
    lv_obj_set_name_static(heading, "preview_heading");
    lv_obj_set_size(heading, LV_PCT(100), 30);
    lv_obj_set_flex_align(heading, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    make_label(heading, "Preview", 22, COLOR_TITLE);
    char count[16]{};
    const auto visible_count = std::min<std::size_t>(app.screenshots.size(), 9);
    std::snprintf(count, sizeof(count), "%zu", visible_count);
    make_label(heading, count, 14, COLOR_SECONDARY);

    auto *gallery = lv_obj_create(section);
    lv_obj_set_name_static(gallery, "screenshot_gallery");
    style_box(gallery, COLOR_PAGE);
    lv_obj_set_style_bg_opa(gallery, LV_OPA_TRANSP, 0);
    lv_obj_set_size(gallery, LV_PCT(100), 360);
    lv_obj_set_style_pad_all(gallery, 0, 0);
    lv_obj_set_style_pad_column(gallery, 12, 0);
    lv_obj_set_flex_flow(gallery, LV_FLEX_FLOW_ROW);
    lv_obj_set_scroll_dir(gallery, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(gallery, LV_SCROLLBAR_MODE_OFF);

    for (std::size_t index = 0; index < visible_count; ++index) {
        const auto &screenshot = app.screenshots[index];
        auto *card             = lv_obj_create(gallery);
        lv_obj_set_name_static(card, "screenshot_card");
        style_box(card, COLOR_SURFACE, 8);
        lv_obj_set_size(card, 166, 360);
        lv_obj_set_style_min_width(card, 166, 0);
        lv_obj_set_style_max_width(card, 166, 0);
        lv_obj_set_flex_grow(card, 0);
        lv_obj_set_style_pad_all(card, 0, 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_border_color(card, lv_color_hex(COLOR_SEPARATOR), 0);
        lv_obj_set_style_clip_corner(card, true, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

        populate_screenshot_card(card, screenshot);
    }
}

void create_facts(lv_obj_t *body, const StoreApp &app)
{
    auto *facts = make_card(body, 60);
    lv_obj_set_name_static(facts, "version_and_size");
    lv_obj_set_style_pad_hor(facts, 16, 0);
    lv_obj_set_style_pad_ver(facts, 0, 0);
    lv_obj_set_flex_flow(facts, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(facts, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    char version[40]{};
    std::snprintf(version, sizeof(version), "Version %s", app.version.c_str());
    auto *version_label = make_label(facts, version, 14, COLOR_SECONDARY);
    lv_obj_set_name_static(version_label, "version_text");
    lv_obj_set_width(version_label, 0);
    lv_obj_set_flex_grow(version_label, 1);
    lv_label_set_long_mode(version_label, LV_LABEL_LONG_DOT);
    auto *size_label = make_label(facts, app.size.c_str(), 14, COLOR_SECONDARY);
    lv_obj_set_name_static(size_label, "size_text");
    lv_obj_set_style_max_width(size_label, LV_PCT(42), 0);
    lv_obj_set_style_text_align(size_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(size_label, LV_LABEL_LONG_DOT);
}

void create_actions(lv_obj_t *body, const StoreApp &app, const DetailViewCallbacks &callbacks)
{
    const char *primary_text = "Open App";
    if (app.busy)
        primary_text = "Working...";
    else if (!app.available)
        primary_text = "Not Compatible";
    else if (is_launcher_package(app.package) && !app.installed)
        primary_text = "System Component";
    else if (!app.installed)
        primary_text = "Get App";
    else if (app.update_available)
        primary_text = "Update App";

    auto *primary = make_button(body, primary_text, 22, COLOR_ACTION, COLOR_SURFACE, callbacks.primary_action);
    lv_obj_set_name_static(primary, "primary_action");
    lv_obj_set_size(primary, LV_PCT(100), 56);
    if (app.busy || !app.available || (is_launcher_package(app.package) && !app.installed) ||
        (app.installed && !app.update_available))
        lv_obj_clear_flag(primary, LV_OBJ_FLAG_CLICKABLE);

    if (!app.installed || !is_removable_package(app.package)) return;
    auto *remove = make_button(body, LV_SYMBOL_TRASH "  Delete App", 22, COLOR_SURFACE, COLOR_DANGER, callbacks.remove);
    lv_obj_set_name_static(remove, "remove_action");
    lv_obj_set_size(remove, LV_PCT(100), 56);
    lv_obj_set_style_border_width(remove, 1, 0);
    lv_obj_set_style_border_color(remove, lv_color_hex(COLOR_DANGER_BORDER), 0);
    if (app.busy) lv_obj_clear_flag(remove, LV_OBJ_FLAG_CLICKABLE);
}

void create_operation_error(lv_obj_t *body, const StoreApp &app)
{
    if (app.operation_error.empty()) return;
    auto *notice = make_flex(body, LV_FLEX_FLOW_ROW);
    lv_obj_set_name_static(notice, "operation_error");
    lv_obj_set_width(notice, LV_PCT(100));
    lv_obj_set_height(notice, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(notice, 12, 0);
    lv_obj_set_style_pad_column(notice, 8, 0);
    style_box(notice, COLOR_DANGER_SOFT, 8);
    make_label(notice, LV_SYMBOL_WARNING, 22, COLOR_DANGER);
    auto *message = make_label(notice, app.operation_error.c_str(), 14, COLOR_DANGER);
    lv_obj_set_width(message, 0);
    lv_obj_set_flex_grow(message, 1);
    lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
}

void create_source_notice(lv_obj_t *body, const StoreApp &app)
{
    auto *notice = make_flex(body, LV_FLEX_FLOW_ROW);
    lv_obj_set_name_static(notice, "source_notice");
    lv_obj_set_width(notice, LV_PCT(100));
    lv_obj_set_height(notice, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(notice, 72, 0);
    lv_obj_set_style_pad_all(notice, 14, 0);
    lv_obj_set_style_pad_column(notice, 10, 0);
    lv_obj_set_flex_align(notice, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    style_box(notice, COLOR_ACCENT_SOFT, 8);
    auto *verified = lv_obj_create(notice);
    style_box(verified, COLOR_ACCENT, LV_RADIUS_CIRCLE);
    lv_obj_set_size(verified, 20, 20);
    lv_obj_set_style_pad_all(verified, 0, 0);
    lv_obj_clear_flag(verified, LV_OBJ_FLAG_SCROLLABLE);
    auto *check = make_label(verified, LV_SYMBOL_OK, 14, COLOR_SURFACE);
    lv_obj_center(check);
    const char *message = app.official ? "This app is published by LILYGO."
                                       : "This community app is distributed through the LILYGO Registry.";
    auto *text          = make_label(notice, message, 14, COLOR_TEXT);
    lv_obj_set_width(text, 0);
    lv_obj_set_flex_grow(text, 1);
    lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
}

const char *permission_name(const std::string &id)
{
    if (id == "network") return "Network";
    if (id == "filesystem") return "File System";
    if (id == "bluetooth") return "Bluetooth";
    if (id == "camera") return "Camera";
    if (id == "microphone") return "Microphone";
    if (id == "location") return "Location";
    if (id == "serial") return "Serial";
    if (id == "gpio") return "GPIO";
    if (id == "i2c") return "I2C";
    if (id == "spi") return "SPI";
    if (id == "imu") return "IMU";
    if (id == "external-display") return "External Display";
    if (id == "background-service") return "Background Service";
    return id.c_str();
}

void create_permissions(lv_obj_t *body, const StoreApp &app)
{
    constexpr std::size_t row_height            = 40;
    constexpr int label_height                  = 34;
    constexpr std::size_t row_gap               = 8;
    constexpr std::size_t card_vertical_padding = 32;
    auto *heading                               = make_label(body, "App Permissions", 22, COLOR_TITLE);
    lv_obj_set_name_static(heading, "permissions_heading");
    const auto rows        = app.permissions.empty() ? 1 : app.permissions.size();
    const auto card_height = static_cast<int>(card_vertical_padding + rows * row_height + (rows - 1) * row_gap);
    auto *card             = make_card(body, card_height);
    lv_obj_set_name_static(card, "permission_summary");
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 8, 0);
    if (app.permissions.empty()) {
        make_label(card, "No additional permissions", 14, COLOR_SUCCESS);
        return;
    }
    for (const auto &permission : app.permissions) {
        auto *row = make_flex(card, LV_FLEX_FLOW_ROW);
        lv_obj_set_size(row, LV_PCT(100), static_cast<int>(row_height));
        lv_obj_set_style_pad_column(row, 4, 0);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        const auto name             = std::string(permission_name(permission.id)) + " ·";
        auto *permission_name_label = make_label(row, name.c_str(), 14, COLOR_TEXT);
        lv_obj_set_width(permission_name_label, LV_PCT(40));
        lv_obj_set_height(permission_name_label, label_height);
        lv_label_set_long_mode(permission_name_label, LV_LABEL_LONG_DOT);
        auto *reason = make_label(row, permission.reason.c_str(), 14, COLOR_SECONDARY);
        lv_obj_set_width(reason, 0);
        lv_obj_set_height(reason, label_height);
        lv_obj_set_flex_grow(reason, 1);
        lv_label_set_long_mode(reason, LV_LABEL_LONG_DOT);
    }
}

}  // namespace

namespace {

void build_detail_view(lv_obj_t *content, DetailViewModel &view_model, const DetailViewCallbacks &callbacks)
{
    const auto *app = view_model.app();
    make_header(content, app ? app->name.c_str() : "App Detail", callbacks.back);
    auto *body = make_page_body(content);
    if (!app) return;

    create_identity(body, *app, view_model.app_index());
    auto *summary = make_label(body, app->summary.c_str(), 22, COLOR_TEXT);
    lv_obj_set_name_static(summary, "app_summary");
    lv_obj_set_width(summary, LV_PCT(100));
    lv_obj_set_style_min_height(summary, 32, 0);
    lv_label_set_long_mode(summary, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(summary, 4, 0);
    create_preview(body, *app);
    create_facts(body, *app);
    create_operation_error(body, *app);
    create_actions(body, *app, callbacks);
    create_source_notice(body, *app);
    create_permissions(body, *app);
}

}  // namespace

DetailView::DetailView(DetailViewModel &view_model, DetailViewCallbacks callbacks) noexcept
    : view_model_(view_model), callbacks_(callbacks)
{
}

lv_obj_t *DetailView::create(lv_obj_t *parent)
{
    destroy();
    surface_ = make_flex(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_name_static(surface_, "detail_page");
    lv_obj_set_size(surface_, LV_PCT(100), LV_PCT(100));
    rebuild();
    suppress_refresh_ = true;
    lv_subject_add_observer_obj(view_model_.revision_subject(), revision_changed, surface_, this);
    suppress_refresh_ = false;
    return surface_;
}

void DetailView::destroy() noexcept
{
    lv_async_call_cancel(refresh_async, this);
    refresh_pending_ = false;
    if (surface_) lv_obj_delete(surface_);
    surface_ = nullptr;
}

void DetailView::revision_changed(lv_observer_t *observer, lv_subject_t *)
{
    auto *view = static_cast<DetailView *>(lv_observer_get_user_data(observer));
    if (view && !view->suppress_refresh_) view->request_refresh();
}

void DetailView::refresh_async(void *user_data)
{
    auto *view             = static_cast<DetailView *>(user_data);
    view->refresh_pending_ = false;
    if (view->surface_) view->rebuild();
}

void DetailView::request_refresh() noexcept
{
    if (!surface_ || refresh_pending_) return;
    refresh_pending_ = lv_async_call(refresh_async, this) == LV_RESULT_OK;
}

void DetailView::rebuild()
{
    int body_scroll_y    = 0;
    int gallery_scroll_x = 0;
    if (auto *body = lv_obj_find_by_name(surface_, "page_body")) body_scroll_y = lv_obj_get_scroll_y(body);
    if (auto *gallery = lv_obj_find_by_name(surface_, "screenshot_gallery"))
        gallery_scroll_x = lv_obj_get_scroll_x(gallery);

    lv_obj_clean(surface_);
    build_detail_view(surface_, view_model_, callbacks_);
    if (callbacks_.layout_changed) callbacks_.layout_changed();
    lv_obj_update_layout(surface_);
    if (auto *body = lv_obj_find_by_name(surface_, "page_body")) lv_obj_scroll_to_y(body, body_scroll_y, LV_ANIM_OFF);
    if (auto *gallery = lv_obj_find_by_name(surface_, "screenshot_gallery"))
        lv_obj_scroll_to_x(gallery, gallery_scroll_x, LV_ANIM_OFF);
}
