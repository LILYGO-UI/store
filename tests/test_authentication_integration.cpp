#include "system/authentication_agent.hpp"
#include "system/process.hpp"

#include <iostream>
#include <string>

#if defined(__linux__)

#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <deque>
#include <fcntl.h>
#include <mutex>
#include <poll.h>
#include <stdexcept>
#include <termios.h>
#include <thread>
#include <unistd.h>
#include <utility>

namespace {

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
volatile std::sig_atomic_t interrupted = 0;

void require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

class InterruptHandler {
public:
    InterruptHandler()
    {
        struct sigaction action{};
        action.sa_handler = +[](int) { interrupted = 1; };
        sigemptyset(&action.sa_mask);
        require(sigaction(SIGINT, &action, &previous_interrupt_) == 0, "Unable to handle terminal interruption");
        if (sigaction(SIGTERM, &action, &previous_terminate_) != 0) {
            sigaction(SIGINT, &previous_interrupt_, nullptr);
            throw std::runtime_error("Unable to handle test termination");
        }
    }
    ~InterruptHandler()
    {
        sigaction(SIGTERM, &previous_terminate_, nullptr);
        sigaction(SIGINT, &previous_interrupt_, nullptr);
    }
    InterruptHandler(const InterruptHandler &)            = delete;
    InterruptHandler &operator=(const InterruptHandler &) = delete;

private:
    struct sigaction previous_interrupt_{};
    struct sigaction previous_terminate_{};
};

class Response {
public:
    Response()
    {
        value.reserve(4096);
    }
    ~Response()
    {
        volatile char *bytes = value.empty() ? nullptr : &value[0];
        for (std::size_t index = 0; index < value.size(); ++index) bytes[index] = '\0';
    }
    Response(const Response &)            = delete;
    Response &operator=(const Response &) = delete;
    std::string value;
};

struct Events {
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<AuthenticationPrompt> prompts;
    std::thread::id callback_thread;
    std::size_t prompt_count      = 0;
    std::size_t completion_count  = 0;
    bool callback_thread_mismatch = false;
    bool process_done             = false;
    ProcessResult result;

    void check_callback_thread()
    {
        const auto current = std::this_thread::get_id();
        if (callback_thread == std::thread::id{}) callback_thread = current;
        callback_thread_mismatch |= callback_thread != current;
    }
};

class Terminal {
public:
    Terminal() : descriptor_(open("/dev/tty", O_RDWR | O_CLOEXEC | O_NOCTTY))
    {
        require(descriptor_ >= 0, "A controlling terminal is required; use an interactive terminal or ssh -t");
        if (tcgetattr(descriptor_, &original_) != 0) {
            close(descriptor_);
            descriptor_ = -1;
            throw std::runtime_error("Unable to read terminal attributes");
        }
    }
    ~Terminal()
    {
        restore();
        if (descriptor_ >= 0) close(descriptor_);
    }
    Terminal(const Terminal &)            = delete;
    Terminal &operator=(const Terminal &) = delete;

    void restore() noexcept
    {
        if (attributes_changed_) {
            tcsetattr(descriptor_, TCSANOW, &original_);
            attributes_changed_ = false;
        }
    }

    void read_response(const AuthenticationPrompt &prompt, Response &response, Events &events,
                       Clock::time_point deadline)
    {
        auto settings = original_;
        if (!prompt.echo_on) settings.c_lflag &= ~(ECHO | ECHONL);
        require(tcsetattr(descriptor_, TCSANOW, &settings) == 0, "Unable to configure terminal input");
        attributes_changed_ = true;
        try {
            const std::string label = "Authenticate as " + prompt.identity + " — " + prompt.prompt + " ";
            write_text(label);
            for (;;) {
                require(!interrupted, "Terminal input was interrupted");
                require(Clock::now() < deadline, "Timed out waiting for terminal input");
                {
                    std::lock_guard<std::mutex> lock(events.mutex);
                    require(!events.process_done && events.completion_count == 0,
                            "Authentication ended before a response was entered");
                }
                pollfd descriptor{descriptor_, POLLIN, 0};
                const auto ready = poll(&descriptor, 1, 100);
                if (ready < 0 && errno == EINTR) continue;
                require(ready >= 0, "Unable to poll the terminal");
                if (ready == 0) continue;
                require((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) == 0, "The controlling terminal closed");
                char character   = '\0';
                const auto count = read(descriptor_, &character, 1);
                if (count < 0 && errno == EINTR) continue;
                require(count == 1, "Terminal input was cancelled or reached EOF");
                if (character == '\n' || character == '\r') break;
                if (character == '\3' || character == '\4' || character == '\32') {
                    character = '\0';
                    throw std::runtime_error("Terminal input was cancelled");
                }
                require(character != '\0' && response.value.size() < 4096, "Invalid or oversized terminal response");
                response.value.push_back(character);
                volatile char *wipe = &character;
                *wipe               = '\0';
            }
            if (!prompt.echo_on) write_text("\n");
            restore();
        } catch (...) {
            restore();
            write_text("\n");
            throw;
        }
    }

private:
    void write_text(const std::string &text)
    {
        std::size_t written = 0;
        while (written < text.size()) {
            const auto count = write(descriptor_, text.data() + written, text.size() - written);
            if (count < 0 && errno == EINTR) continue;
            require(count > 0, "Unable to write to the terminal");
            written += static_cast<std::size_t>(count);
        }
    }

    int descriptor_;
    termios original_{};
    bool attributes_changed_ = false;
};

// Only the process runner thread executes pkexec: its parent stays the exact
// Unix process registered by AuthenticationAgent, with no intervening shell.
class Probe {
public:
    Probe(AuthenticationAgent &agent, Events &events, Terminal &terminal)
        : agent_(agent), events_(events), terminal_(terminal), worker_([&events] {
              ProcessResult result;
              try {
                  PosixProcessRunner runner;
                  result = runner.run({"/usr/bin/pkexec", "--disable-internal-agent", "/usr/bin/true"}, 16384);
              } catch (...) {
                  result.exit_code = -1;
              }
              {
                  std::lock_guard<std::mutex> lock(events.mutex);
                  events.result       = std::move(result);
                  events.process_done = true;
              }
              events.changed.notify_all();
          })
    {
    }

    ~Probe()
    {
        terminal_.restore();
        agent_.stop();
        std::unique_lock<std::mutex> lock(events_.mutex);
        if (!events_.changed.wait_for(lock, 10s, [&] { return events_.process_done; })) {
            // The project runner deliberately has no PID/cancellation API. A
            // broken polkit daemon must not leave a joinable thread referring
            // to destroyed test data. No privileged mutation is running.
            std::cerr << "FAIL: pkexec did not exit after stopping its authentication agent\n";
            std::_Exit(EXIT_FAILURE);
        }
        lock.unlock();
        worker_.join();
    }

    Probe(const Probe &)            = delete;
    Probe &operator=(const Probe &) = delete;

private:
    AuthenticationAgent &agent_;
    Events &events_;
    Terminal &terminal_;
    std::thread worker_;
};

void run_test(bool cancel)
{
    require(geteuid() != 0 && getuid() != 0, "Run this test as the ordinary desktop user, without sudo");
    InterruptHandler interrupt_handler;
    Events events;
    Terminal terminal;
    AuthenticationAgent agent(
        [&](AuthenticationPrompt prompt) {
            {
                std::lock_guard<std::mutex> lock(events.mutex);
                events.check_callback_thread();
                ++events.prompt_count;
                events.prompts.push_back(std::move(prompt));
            }
            events.changed.notify_all();
        },
        [&] {
            {
                std::lock_guard<std::mutex> lock(events.mutex);
                events.check_callback_thread();
                ++events.completion_count;
            }
            events.changed.notify_all();
        });
    std::string error;
    if (!agent.start(error)) throw std::runtime_error("Unable to register authentication agent: " + error);

    std::size_t responses        = 0;
    std::size_t secret_responses = 0;
    bool cancellation_requested  = false;
    std::uint64_t handled_id     = 0;
    const auto deadline          = Clock::now() + 120s;
    {
        Probe probe(agent, events, terminal);
        for (;;) {
            require(!interrupted, "Authentication test was interrupted");
            AuthenticationPrompt prompt;
            {
                std::unique_lock<std::mutex> lock(events.mutex);
                if (!events.changed.wait_for(lock, 100ms,
                                             [&] { return events.process_done || !events.prompts.empty(); })) {
                    require(Clock::now() < deadline, "Timed out waiting for authentication callbacks");
                    continue;
                }
                if (events.process_done) break;
                prompt = std::move(events.prompts.front());
                events.prompts.pop_front();
            }
            if (!prompt.awaiting_response || prompt.id == handled_id || cancellation_requested) continue;
            require(prompt.id != 0, "Authentication request has no identifier");
            handled_id = prompt.id;
            if (cancel) {
                cancellation_requested = true;
                agent.cancel(prompt.id);
                continue;
            }
            // This manual probe never retries a failed password. Cancel a
            // second secret challenge to avoid accidentally locking an account.
            require(prompt.echo_on || secret_responses == 0,
                    "A second secret was requested; cancelling without another password attempt");
            require(responses < 4, "Too many authentication prompts");
            Response response;
            terminal.read_response(prompt, response, events, deadline);
            agent.respond(prompt.id, response.value);
            ++responses;
            if (!prompt.echo_on) ++secret_responses;
        }
    }
    // Probe has joined the process worker and stopped/joined the agent, so all
    // callback counters below are stable; no callback reads terminal input.
    require(events.prompt_count > 0 && handled_id != 0,
            "No authentication prompt was observed; policy or cached authorization skipped the conversation");
    require(events.completion_count == 1, "Expected exactly one authentication completion callback");
    require(!events.callback_thread_mismatch && events.callback_thread != std::this_thread::get_id(),
            "Authentication callbacks were not confined to their worker thread");
    require(!events.result.output_limit_exceeded, "pkexec output exceeded the test limit");
    if (cancel) {
        require(cancellation_requested && responses == 0, "Cancellation mode submitted an authentication response");
        // pkexec uses 126 for dismissed dialogs, and 127 when an agent reports
        // a completed conversation without granting authorization.
        require(events.result.exit_code == 126 || events.result.exit_code == 127,
                "Cancelled authentication did not deny pkexec authorization");
    } else {
        require(responses > 0 && events.result.exit_code == 0, "Authentication did not successfully execute true");
    }
    std::cout << "PASS: " << (cancel ? "cancellation" : "success") << ", pkexec exit=" << events.result.exit_code
              << ", prompt callbacks=" << events.prompt_count << ", completion callbacks=" << events.completion_count
              << '\n';
}

}  // namespace
#endif

int main(int argc, char **argv)
{
    if (argc != 2 || (std::string(argv[1]) != "--success" && std::string(argv[1]) != "--cancel")) {
        std::cerr << "Usage: " << argv[0] << " --success|--cancel\n"
                  << "Manual Linux test: ordinary user, real controlling terminal, running polkit.\n"
                  << "Only /usr/bin/true is executed with authorization. Responses are read from /dev/tty.\n";
        return 2;
    }
#if defined(__linux__)
    try {
        run_test(std::string(argv[1]) == "--cancel");
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << "FAIL: " << failure.what() << '\n';
        return 1;
    }
#else
    std::cout << "SKIP: native polkit authentication requires Linux\n";
    return 77;
#endif
}
