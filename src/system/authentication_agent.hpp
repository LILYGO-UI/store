#ifndef LILYGO_UI_STORE_AUTHENTICATION_AGENT_HPP
#define LILYGO_UI_STORE_AUTHENTICATION_AGENT_HPP

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

struct AuthenticationPrompt {
    std::uint64_t id = 0;
    std::string message;
    std::string identity;
    std::string prompt;
    std::string error;
    bool echo_on           = false;
    bool awaiting_response = true;
};

// Callbacks run on the authentication thread. They must post ordinary data to
// the UI, never call LVGL, stop(), or destroy this object directly. start()
// waits for registration, so call it outside the UI thread before spawning
// pkexec. stop() may run concurrently with start(), permanently closes this
// instance, and waits for the authentication thread to finish.
class AuthenticationAgent {
public:
    AuthenticationAgent(std::function<void(AuthenticationPrompt)> on_request, std::function<void()> on_complete);
    ~AuthenticationAgent();

    AuthenticationAgent(const AuthenticationAgent &)            = delete;
    AuthenticationAgent &operator=(const AuthenticationAgent &) = delete;

    bool start(std::string &error);
    // The owned response is wiped after use. Callers must wipe their own copy
    // and avoid moving short strings that can retain bytes in inline storage.
    void respond(std::uint64_t id, std::string response) noexcept;
    void cancel(std::uint64_t id) noexcept;
    void stop() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

#endif
