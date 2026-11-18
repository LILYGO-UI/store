#include "system/authentication_agent.hpp"

#include <utility>

namespace {

void erase_response(std::string &response) noexcept
{
    volatile char *data = response.empty() ? nullptr : &response[0];
    for (std::size_t index = 0; index < response.size(); ++index) data[index] = '\0';
    response.clear();
}

struct ResponseWiper {
    std::string &value;
    ~ResponseWiper()
    {
        erase_response(value);
    }
};

}  // namespace

#if defined(__linux__)

#define POLKIT_AGENT_I_KNOW_API_IS_SUBJECT_TO_CHANGE 1
#include <polkitagent/polkitagent.h>

#include <condition_variable>
#include <exception>
#include <mutex>
#include <pwd.h>
#include <sys/prctl.h>
#include <thread>
#include <unistd.h>

namespace {

struct AgentState;

typedef struct _StoreAuthenticationListener {
    PolkitAgentListener parent;
    AgentState *state;
} StoreAuthenticationListener;

typedef struct _StoreAuthenticationListenerClass {
    PolkitAgentListenerClass parent_class;
} StoreAuthenticationListenerClass;

G_DEFINE_TYPE(StoreAuthenticationListener, store_authentication_listener, POLKIT_AGENT_TYPE_LISTENER)

struct Response {
    explicit Response(const std::string &input) : value(input)
    {
    }
    ~Response()
    {
        erase_response(value);
    }
    Response(const Response &)            = delete;
    Response &operator=(const Response &) = delete;
    std::string value;
};

struct AgentState {
    std::function<void(AuthenticationPrompt)> on_request;
    std::function<void()> on_complete;

    std::mutex lifecycle_mutex;
    std::mutex mutex;
    std::condition_variable startup_condition;
    std::thread worker;
    GMainContext *context             = nullptr;
    GCancellable *startup_cancellable = nullptr;
    bool startup_done                 = false;
    bool running                      = false;
    bool stopping                     = false;
    gchar *startup_error              = nullptr;

    // The following fields belong exclusively to the private GLib context.
    GMainLoop *loop                          = nullptr;
    StoreAuthenticationListener *listener    = nullptr;
    gpointer registration                    = nullptr;
    PolkitAgentSession *session              = nullptr;
    GTask *task                              = nullptr;
    GCancellable *authentication_cancellable = nullptr;
    gulong cancellation_handler              = 0;
    AuthenticationPrompt prompt;
    std::uint64_t next_request_id = 0;
    std::uint64_t conversation_id = 0;
    bool shutdown_scheduled       = false;

    struct PendingCallback {
        AgentState *state;
        std::function<void()> callback;
    };

    struct Cancellation {
        AgentState *state;
        std::uint64_t conversation;
    };

    AgentState(std::function<void(AuthenticationPrompt)> request, std::function<void()> complete)
        : on_request(std::move(request)), on_complete(std::move(complete))
    {
    }

    bool is_stopping()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return stopping;
    }

    bool post(std::function<void()> callback, bool during_shutdown = false)
    {
        auto function        = std::make_unique<PendingCallback>(PendingCallback{this, std::move(callback)});
        GMainContext *target = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!context || (stopping && !during_shutdown)) return false;
            target = g_main_context_ref(context);
        }
        auto *source = g_idle_source_new();
        g_source_set_callback(
            source,
            [](gpointer data) -> gboolean {
                auto *pending = static_cast<PendingCallback *>(data);
                try {
                    pending->callback();
                } catch (...) {
                    // Never unwind a C++ exception through GLib dispatch.
                    pending->state->finish_authentication(true, "The authentication handler failed");
                }
                return G_SOURCE_REMOVE;
            },
            function.release(), [](gpointer data) { delete static_cast<PendingCallback *>(data); });
        g_source_attach(source, target);
        g_source_unref(source);
        g_main_context_unref(target);
        return true;
    }

    void emit_prompt() noexcept
    {
        try {
            if (on_request) on_request(prompt);
        } catch (...) {
            // A failed UI delivery must not leave an unanswerable conversation.
            const auto conversation = conversation_id;
            try {
                post([this, conversation] {
                    if (conversation_id == conversation) cancel_current();
                });
            } catch (...) {
            }
        }
    }

    void emit_complete() noexcept
    {
        try {
            if (on_complete) on_complete();
        } catch (...) {
        }
    }

    void finish_authentication(bool cancel_session = false, const char *failure = nullptr) noexcept
    {
        if (!task) return;
        auto *finished_task = task;
        task                = nullptr;
        if (session) {
            g_signal_handlers_disconnect_by_data(session, this);
            if (cancel_session) polkit_agent_session_cancel(session);
            g_clear_object(&session);
        }
        if (authentication_cancellable) {
            if (cancellation_handler) g_cancellable_disconnect(authentication_cancellable, cancellation_handler);
            cancellation_handler = 0;
            g_clear_object(&authentication_cancellable);
        }
        prompt          = {};
        conversation_id = 0;
        emit_complete();
        // The authority learns the authorization result from PolkitAgentSession,
        // not from this boolean. It means the agent handled the conversation.
        if (!g_task_return_error_if_cancelled(finished_task)) {
            if (failure)
                g_task_return_new_error(finished_task, POLKIT_ERROR, POLKIT_ERROR_FAILED, "%s", failure);
            else
                g_task_return_boolean(finished_task, TRUE);
        }
        g_object_unref(finished_task);
    }

    void cancel_current() noexcept
    {
        finish_authentication(true);
    }

    void shutdown() noexcept
    {
        if (shutdown_scheduled) return;
        shutdown_scheduled = true;
        cancel_current();
        // GTask replies are delivered on this context. Allow them to run before
        // unregistering the agent, otherwise pkexec can wait for a lost reply.
        auto *source = g_idle_source_new();
        g_source_set_priority(source, G_PRIORITY_LOW);
        g_source_set_callback(
            source,
            [](gpointer data) -> gboolean {
                g_main_loop_quit(static_cast<GMainLoop *>(data));
                return G_SOURCE_REMOVE;
            },
            g_main_loop_ref(loop), [](gpointer data) { g_main_loop_unref(static_cast<GMainLoop *>(data)); });
        g_source_attach(source, context);
        g_source_unref(source);
    }

    void request_shutdown()
    {
        GMainContext *target = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!context) return;
            target = g_main_context_ref(context);
        }
        // stop() must still join its worker if allocating a C++ closure fails.
        // This shutdown source only carries a borrowed pointer kept alive by
        // that join; GLib owns the source until it runs or the context closes.
        auto *source = g_idle_source_new();
        g_source_set_callback(
            source,
            [](gpointer data) -> gboolean {
                static_cast<AgentState *>(data)->shutdown();
                return G_SOURCE_REMOVE;
            },
            this, nullptr);
        g_source_attach(source, target);
        g_source_unref(source);
        g_main_context_unref(target);
    }

    static std::string identity_name(PolkitIdentity *identity)
    {
        const uid_t uid = polkit_unix_user_get_uid(POLKIT_UNIX_USER(identity));
        struct passwd record{};
        struct passwd *found = nullptr;
        char buffer[4096]{};
        if (getpwuid_r(uid, &record, buffer, sizeof(buffer), &found) == 0 && found && found->pw_name)
            return found->pw_name;
        return "uid " + std::to_string(uid);
    }

    std::uint64_t new_request_id() noexcept
    {
        if (++next_request_id == 0) ++next_request_id;
        return next_request_id;
    }

    template <typename Callback>
    void dispatch(Callback callback) noexcept
    {
        try {
            callback();
        } catch (...) {
            finish_authentication(true, "The authentication handler failed");
        }
    }

    void begin_authentication(PolkitAgentListener *source, const gchar *message, const gchar *cookie, GList *identities,
                              GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data) noexcept
    {
        auto *pending = g_task_new(source, cancellable, callback, user_data);
        try {
            if (task || is_stopping()) {
                g_task_return_new_error(pending, POLKIT_ERROR, POLKIT_ERROR_FAILED,
                                        "The authentication agent is busy or closing");
                g_object_unref(pending);
                return;
            }
            PolkitIdentity *identity = nullptr;
            for (auto *entry = identities; entry; entry = entry->next) {
                auto *candidate = POLKIT_IDENTITY(entry->data);
                if (!POLKIT_IS_UNIX_USER(candidate)) continue;
                if (!identity) identity = candidate;
                if (static_cast<uid_t>(polkit_unix_user_get_uid(POLKIT_UNIX_USER(candidate))) == getuid()) {
                    identity = candidate;
                    break;
                }
            }
            if (!identity) {
                g_task_return_new_error(pending, POLKIT_ERROR, POLKIT_ERROR_FAILED,
                                        "No supported administrator identity was offered");
                g_object_unref(pending);
                return;
            }

            task                     = pending;
            prompt                   = {};
            conversation_id          = new_request_id();
            prompt.id                = conversation_id;
            prompt.message           = message ? message : "Administrator authentication is required";
            prompt.identity          = identity_name(identity);
            prompt.awaiting_response = false;
            session                  = polkit_agent_session_new(identity, cookie);
            g_signal_connect(
                session, "request",
                G_CALLBACK(+[](PolkitAgentSession *, const gchar *request, gboolean echo_on, gpointer data) {
                    auto *self = static_cast<AgentState *>(data);
                    self->dispatch([&] {
                        self->prompt.id                = self->new_request_id();
                        self->prompt.prompt            = request ? request : "Password:";
                        self->prompt.echo_on           = echo_on;
                        self->prompt.awaiting_response = true;
                        self->emit_prompt();
                    });
                }),
                this);
            g_signal_connect(session, "show-error",
                             G_CALLBACK(+[](PolkitAgentSession *, const gchar *text, gpointer data) {
                                 auto *self = static_cast<AgentState *>(data);
                                 self->dispatch([&] {
                                     self->prompt.error = text ? text : "Authentication failed";
                                     self->emit_prompt();
                                 });
                             }),
                             this);
            g_signal_connect(session, "show-info",
                             G_CALLBACK(+[](PolkitAgentSession *, const gchar *text, gpointer data) {
                                 auto *self = static_cast<AgentState *>(data);
                                 self->dispatch([&] {
                                     self->prompt.error = text ? text : "";
                                     self->emit_prompt();
                                 });
                             }),
                             this);
            g_signal_connect(session, "completed", G_CALLBACK(+[](PolkitAgentSession *, gboolean, gpointer data) {
                                 static_cast<AgentState *>(data)->finish_authentication();
                             }),
                             this);
            if (cancellable) {
                authentication_cancellable = G_CANCELLABLE(g_object_ref(cancellable));
                auto *cancellation         = new Cancellation{this, conversation_id};
                cancellation_handler       = g_cancellable_connect(
                    cancellable, G_CALLBACK(+[](GCancellable *, gpointer data) {
                        auto *cancellation      = static_cast<Cancellation *>(data);
                        auto *self              = cancellation->state;
                        const auto conversation = cancellation->conversation;
                        try {
                            self->post(
                                [self, conversation] {
                                    if (self->conversation_id == conversation) self->cancel_current();
                                },
                                true);
                        } catch (...) {
                        }
                    }),
                    cancellation, [](gpointer data) { delete static_cast<Cancellation *>(data); });
            }
            polkit_agent_session_initiate(session);
        } catch (...) {
            if (task == pending) {
                finish_authentication(true, "The authentication agent could not start the conversation");
            } else {
                g_task_return_new_error(pending, POLKIT_ERROR, POLKIT_ERROR_FAILED,
                                        "The authentication agent could not start the conversation");
                g_object_unref(pending);
            }
        }
    }

    void run() noexcept
    {
        auto *private_context = g_main_context_new();
        g_main_context_push_thread_default(private_context);
        loop = g_main_loop_new(private_context, FALSE);
        {
            std::lock_guard<std::mutex> lock(mutex);
            context = private_context;
        }
        listener =
            static_cast<StoreAuthenticationListener *>(g_object_new(store_authentication_listener_get_type(), nullptr));
        listener->state = this;
        auto *subject   = polkit_unix_process_new_for_owner(getpid(), 0, getuid());
        GError *error   = nullptr;
        registration = polkit_agent_listener_register(POLKIT_AGENT_LISTENER(listener), POLKIT_AGENT_REGISTER_FLAGS_NONE,
                                                      subject, nullptr, startup_cancellable, &error);
        g_object_unref(subject);
        {
            std::lock_guard<std::mutex> lock(mutex);
            startup_done = true;
            running      = registration != nullptr && !stopping;
            g_clear_pointer(&startup_error, g_free);
            if (!running)
                startup_error = g_strdup(error ? error->message : "Authentication agent startup was cancelled");
        }
        g_clear_error(&error);
        startup_condition.notify_all();
        if (registration) {
            if (is_stopping()) shutdown();
            g_main_loop_run(loop);
            polkit_agent_listener_unregister(registration);
            registration = nullptr;
        }
        cancel_current();
        g_clear_object(&listener);
        {
            std::lock_guard<std::mutex> lock(mutex);
            context = nullptr;
            running = false;
        }
        g_main_loop_unref(loop);
        loop = nullptr;
        g_main_context_pop_thread_default(private_context);
        g_main_context_unref(private_context);
    }

    bool start(std::string &error)
    {
        {
            std::lock_guard<std::mutex> lifecycle(lifecycle_mutex);
            bool previous_finished = false;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (stopping) {
                    error = "The authentication agent is closed";
                    return false;
                }
                if (running && !stopping) {
                    error.clear();
                    return true;
                }
                previous_finished = startup_done;
            }
            if (worker.joinable() && previous_finished) worker.join();
            if (!worker.joinable()) {
                if (geteuid() != 0 && prctl(PR_GET_NO_NEW_PRIVS, 0, 0, 0, 0) == 1) {
                    error =
                        "This application session blocks administrator authentication (NoNewPrivileges). "
                        "Restart Store from an updated Launcher.";
                    return false;
                }
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    g_clear_object(&startup_cancellable);
                    startup_cancellable = g_cancellable_new();
                    startup_done        = false;
                    shutdown_scheduled  = false;
                    g_clear_pointer(&startup_error, g_free);
                }
                try {
                    worker = std::thread([this] { run(); });
                } catch (const std::exception &failure) {
                    std::lock_guard<std::mutex> lock(mutex);
                    startup_done  = true;
                    startup_error = g_strdup(failure.what());
                    error         = startup_error;
                    startup_condition.notify_all();
                    return false;
                }
            }
        }
        std::unique_lock<std::mutex> lock(mutex);
        startup_condition.wait(lock, [this] { return startup_done || stopping; });
        error = stopping ? "The authentication agent is closed" : (startup_error ? startup_error : "");
        return running && !stopping;
    }

    void stop() noexcept
    {
        try {
            std::lock_guard<std::mutex> lifecycle(lifecycle_mutex);
            GCancellable *startup = nullptr;
            {
                std::lock_guard<std::mutex> lock(mutex);
                stopping = true;
                if (startup_cancellable) startup = G_CANCELLABLE(g_object_ref(startup_cancellable));
            }
            startup_condition.notify_all();
            if (startup) {
                g_cancellable_cancel(startup);
                g_object_unref(startup);
            }
            request_shutdown();
            if (worker.joinable() && worker.get_id() != std::this_thread::get_id()) worker.join();
            {
                std::lock_guard<std::mutex> lock(mutex);
                g_clear_object(&startup_cancellable);
                g_clear_pointer(&startup_error, g_free);
            }
        } catch (...) {
        }
    }
};

void store_authentication_listener_init(StoreAuthenticationListener *listener)
{
    listener->state = nullptr;
}

void store_authentication_listener_class_init(StoreAuthenticationListenerClass *klass)
{
    auto *listener_class = POLKIT_AGENT_LISTENER_CLASS(klass);
    listener_class->initiate_authentication =
        +[](PolkitAgentListener *listener, const gchar *, const gchar *message, const gchar *, PolkitDetails *,
            const gchar *cookie, GList *identities, GCancellable *cancellable, GAsyncReadyCallback callback,
            gpointer user_data) {
            auto *self = reinterpret_cast<StoreAuthenticationListener *>(listener);
            self->state->begin_authentication(listener, message, cookie, identities, cancellable, callback, user_data);
        };
    listener_class->initiate_authentication_finish =
        +[](PolkitAgentListener *listener, GAsyncResult *result, GError **error) -> gboolean {
        g_return_val_if_fail(g_task_is_valid(result, listener), FALSE);
        return g_task_propagate_boolean(G_TASK(result), error);
    };
}

}  // namespace

struct AuthenticationAgent::Impl : AgentState {
    using AgentState::AgentState;
};

#else

#include <atomic>

struct AuthenticationAgent::Impl {
    std::atomic<bool> stopped{false};
    Impl(std::function<void(AuthenticationPrompt)>, std::function<void()>)
    {
    }
    bool start(std::string &error)
    {
        if (stopped.load()) {
            error = "The authentication agent is closed";
            return false;
        }
        error.clear();
        return true;
    }
    void stop() noexcept
    {
        stopped.store(true);
    }
};

#endif

AuthenticationAgent::AuthenticationAgent(std::function<void(AuthenticationPrompt)> on_request,
                                         std::function<void()> on_complete)
    : impl_(std::make_unique<Impl>(std::move(on_request), std::move(on_complete)))
{
}

AuthenticationAgent::~AuthenticationAgent()
{
    stop();
}

bool AuthenticationAgent::start(std::string &error)
{
    return impl_->start(error);
}

void AuthenticationAgent::respond(std::uint64_t id, std::string response) noexcept
{
    ResponseWiper wiper{response};
#if defined(__linux__)
    try {
        if (response.size() > 4096 || response.find('\0') != std::string::npos ||
            response.find('\n') != std::string::npos) {
            erase_response(response);
            cancel(id);
            return;
        }
        // Copy before wiping: moving a short string can leave its old inline bytes
        // in a moved-from string whose zero size prevents a subsequent wipe.
        auto secret = std::make_shared<Response>(response);
        erase_response(response);
        impl_->post([state = impl_.get(), id, secret] {
            if (state->is_stopping() || !state->session || state->prompt.id != id || !state->prompt.awaiting_response)
                return;
            state->prompt.awaiting_response = false;
            state->prompt.error.clear();
            state->emit_prompt();
            polkit_agent_session_response(state->session, secret->value.c_str());
            erase_response(secret->value);
        });
    } catch (...) {
        cancel(id);
    }
#else
    (void)id;
#endif
}

void AuthenticationAgent::cancel(std::uint64_t id) noexcept
{
#if defined(__linux__)
    try {
        impl_->post([state = impl_.get(), id] {
            if (state->session && state->prompt.id == id) state->cancel_current();
        });
    } catch (...) {
    }
#else
    (void)id;
#endif
}

void AuthenticationAgent::stop() noexcept
{
    impl_->stop();
}
