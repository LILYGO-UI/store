#include "components/authentication_view.hpp"
#include "components/components.hpp"

#include <cm0/input.h>
#include <cm0/typography.h>
#include <algorithm>
#include <utility>

namespace {
lv_obj_t *text(lv_obj_t *parent, const char *name, int size, std::uint32_t color)
{
    auto *label = make_label(parent, "", size, color);
    lv_obj_set_name_static(label, name);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    return label;
}

AuthenticationView *view(lv_event_t *event)
{
    return static_cast<AuthenticationView *>(lv_event_get_user_data(event));
}
}  // namespace

AuthenticationView::AuthenticationView(std::function<void(std::uint64_t, const std::string &)> respond,
                                       std::function<void(std::uint64_t)> cancel)
    : respond_(std::move(respond)), cancel_(std::move(cancel))
{
}

AuthenticationView::~AuthenticationView()
{
    destroy();
}

void AuthenticationView::create(lv_obj_t *parent)
{
    root_ = make_flex(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_name_static(root_, "authentication_dialog");
    style_box(root_, COLOR_PAGE);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_align(root_, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_pad_all(root_, 12, 0);
    // The software keyboard must stay above AppKit's Home Indicator.
    lv_obj_set_style_pad_bottom(root_, 50, 0);
    lv_obj_set_style_pad_row(root_, 8, 0);
    lv_obj_set_flex_align(root_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(root_, size_event, LV_EVENT_SIZE_CHANGED, this);
    lv_obj_add_event_cb(root_, delete_event, LV_EVENT_DELETE, this);
    cm0_input_set_focus_scope(root_);

    auto *body = make_flex(root_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_name_static(body, "authentication_body");
    lv_obj_set_size(body, LV_PCT(100), 0);
    lv_obj_set_style_max_width(body, 520, 0);
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_style_pad_row(body, 10, 0);
    lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);
    auto *title = text(body, "authentication_title", 28, COLOR_TITLE);
    lv_label_set_text(title, "Administrator");
    message_  = text(body, "authentication_message", 14, COLOR_SECONDARY);
    identity_ = text(body, "authentication_identity", 22, COLOR_TEXT);
    prompt_   = text(body, "authentication_prompt", 22, COLOR_TEXT);
    entry_    = lv_textarea_create(body);
    lv_obj_set_name_static(entry_, "authentication_entry");
    lv_obj_set_size(entry_, LV_PCT(100), 48);
    lv_textarea_set_one_line(entry_, true);
    lv_textarea_set_max_length(entry_, 512);
    lv_textarea_set_password_show_time(entry_, 0);
    style_box(entry_, COLOR_SURFACE, 8);
    lv_obj_set_style_text_color(entry_, lv_color_hex(COLOR_TEXT), 0);
    if (const auto *font = lilygo_ui_font_get(22)) lv_obj_set_style_text_font(entry_, font, 0);
    lv_obj_add_event_cb(entry_, submit_event, LV_EVENT_READY, this);
    error_ = text(body, "authentication_error", 14, COLOR_DANGER);

    auto *actions = make_flex(root_, LV_FLEX_FLOW_ROW);
    lv_obj_set_name_static(actions, "authentication_actions");
    lv_obj_set_size(actions, LV_PCT(100), 48);
    lv_obj_set_style_max_width(actions, 520, 0);
    lv_obj_set_style_pad_column(actions, 8, 0);
    auto *toggle = make_button(actions, LV_SYMBOL_KEYBOARD, 22, COLOR_CONTROL, COLOR_TEXT, toggle_event, this);
    lv_obj_set_name_static(toggle, "authentication_keyboard_toggle");
    lv_obj_set_size(toggle, 44, 48);
    auto *cancel = make_button(actions, "Cancel", 22, COLOR_CONTROL, COLOR_TEXT, cancel_event, this);
    lv_obj_set_name_static(cancel, "authentication_cancel");
    lv_obj_set_size(cancel, 0, 48);
    lv_obj_set_flex_grow(cancel, 1);
    submit_ = make_button(actions, "Continue", 22, COLOR_ACTION, COLOR_SURFACE, submit_event, this);
    lv_obj_set_name_static(submit_, "authentication_submit");
    lv_obj_set_size(submit_, 0, 48);
    lv_obj_set_flex_grow(submit_, 1);
    lv_obj_set_style_bg_color(submit_, lv_color_hex(COLOR_DISABLED_SURFACE), LV_STATE_DISABLED);

    keyboard_ = lv_keyboard_create(root_);
    lv_obj_set_name_static(keyboard_, "authentication_keyboard");
    lv_obj_set_width(keyboard_, LV_PCT(100));
    lv_obj_set_style_max_width(keyboard_, 720, 0);
    style_box(keyboard_, COLOR_PAGE);
    if (const auto *font = lilygo_ui_font_get(22)) lv_obj_set_style_text_font(keyboard_, font, LV_PART_ITEMS);
    lv_keyboard_set_textarea(keyboard_, entry_);
    lv_obj_add_event_cb(keyboard_, keyboard_event, LV_EVENT_ALL, this);
    if (cm0_input_hardware_keyboard_present()) lv_obj_add_flag(keyboard_, LV_OBJ_FLAG_HIDDEN);
    resize();
}

void AuthenticationView::show(lv_obj_t *parent, const AuthenticationPrompt &prompt)
{
    if (!root_) create(parent);
    const bool new_request = request_id_ != prompt.id;
    request_id_            = prompt.id;
    awaiting_              = prompt.awaiting_response;
    lv_label_set_text(message_, prompt.message.c_str());
    lv_label_set_text(identity_, ("Authenticate as " + prompt.identity).c_str());
    lv_label_set_text(prompt_, prompt.prompt.empty() ? "Authenticating..." : prompt.prompt.c_str());
    lv_label_set_text(error_, prompt.error.c_str());
    lv_obj_set_flag(error_, LV_OBJ_FLAG_HIDDEN, prompt.error.empty());
    if (new_request || !awaiting_) lv_textarea_set_text(entry_, "");
    lv_textarea_set_password_mode(entry_, !prompt.echo_on);
    lv_obj_set_state(entry_, LV_STATE_DISABLED, !awaiting_);
    lv_obj_set_state(submit_, LV_STATE_DISABLED, !awaiting_);
    lv_obj_set_state(keyboard_, LV_STATE_DISABLED, !awaiting_);
    if (new_request && awaiting_) {
        if (lv_obj_get_group(entry_)) lv_group_focus_obj(entry_);
        lv_obj_update_layout(root_);
        lv_obj_scroll_to_view(entry_, LV_ANIM_OFF);
    }
}

void AuthenticationView::submit()
{
    if (!entry_ || !awaiting_) return;
    std::string response = lv_textarea_get_text(entry_);
    lv_textarea_set_text(entry_, "");
    awaiting_ = false;
    lv_obj_add_state(entry_, LV_STATE_DISABLED);
    lv_obj_add_state(submit_, LV_STATE_DISABLED);
    lv_obj_add_state(keyboard_, LV_STATE_DISABLED);
    if (respond_) respond_(request_id_, response);
    volatile char *secret = response.empty() ? nullptr : &response[0];
    for (std::size_t index = 0; index < response.size(); ++index) secret[index] = '\0';
    response.clear();
}

void AuthenticationView::submit_event(lv_event_t *event)
{
    view(event)->submit();
}
void AuthenticationView::cancel_event(lv_event_t *event)
{
    auto *self = view(event);
    if (self->entry_) lv_textarea_set_text(self->entry_, "");
    self->awaiting_ = false;
    if (self->submit_) lv_obj_add_state(self->submit_, LV_STATE_DISABLED);
    if (self->cancel_) self->cancel_(self->request_id_);
}
void AuthenticationView::keyboard_event(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_READY)
        view(event)->submit();
    else if (lv_event_get_code(event) == LV_EVENT_CANCEL)
        lv_obj_add_flag(view(event)->keyboard_, LV_OBJ_FLAG_HIDDEN);
}
void AuthenticationView::toggle_event(lv_event_t *event)
{
    auto *self = view(event);
    lv_obj_set_flag(self->keyboard_, LV_OBJ_FLAG_HIDDEN, !lv_obj_has_flag(self->keyboard_, LV_OBJ_FLAG_HIDDEN));
    lv_obj_update_layout(self->root_);
    lv_obj_scroll_to_view(self->entry_, LV_ANIM_OFF);
}
void AuthenticationView::size_event(lv_event_t *event)
{
    view(event)->resize();
}
void AuthenticationView::resize()
{
    if (root_ && keyboard_) lv_obj_set_height(keyboard_, std::clamp(lv_obj_get_content_height(root_) / 3, 130, 230));
}
void AuthenticationView::clear_objects()
{
    root_ = message_ = identity_ = prompt_ = error_ = entry_ = submit_ = keyboard_ = nullptr;
    request_id_                                                                    = 0;
    awaiting_                                                                      = false;
}
void AuthenticationView::delete_event(lv_event_t *event)
{
    view(event)->clear_objects();
}
void AuthenticationView::destroy()
{
    if (entry_) lv_textarea_set_text(entry_, "");
    if (root_) lv_obj_delete(root_);
    clear_objects();
}
