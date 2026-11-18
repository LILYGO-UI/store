#ifndef LILYGO_UI_STORE_AUTHENTICATION_VIEW_HPP
#define LILYGO_UI_STORE_AUTHENTICATION_VIEW_HPP

#include "system/authentication_agent.hpp"
#include <lvgl.h>

// Owns a modal subtree. All methods and callbacks run on the LVGL thread.
class AuthenticationView {
public:
    AuthenticationView(std::function<void(std::uint64_t, const std::string &)> respond,
                       std::function<void(std::uint64_t)> cancel);
    ~AuthenticationView();
    void show(lv_obj_t *parent, const AuthenticationPrompt &prompt);
    void destroy();
    bool visible() const
    {
        return root_ != nullptr;
    }

private:
    void create(lv_obj_t *parent);
    void submit();
    void resize();
    static void submit_event(lv_event_t *event);
    static void cancel_event(lv_event_t *event);
    static void keyboard_event(lv_event_t *event);
    static void toggle_event(lv_event_t *event);
    static void size_event(lv_event_t *event);
    static void delete_event(lv_event_t *event);
    void clear_objects();

    std::function<void(std::uint64_t, const std::string &)> respond_;
    std::function<void(std::uint64_t)> cancel_;
    std::uint64_t request_id_ = 0;
    bool awaiting_            = false;
    lv_obj_t *root_           = nullptr;
    lv_obj_t *message_        = nullptr;
    lv_obj_t *identity_       = nullptr;
    lv_obj_t *prompt_         = nullptr;
    lv_obj_t *error_          = nullptr;
    lv_obj_t *entry_          = nullptr;
    lv_obj_t *submit_         = nullptr;
    lv_obj_t *keyboard_       = nullptr;
};

#endif
