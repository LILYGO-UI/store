#include "system/process.hpp"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>

extern char **environ;

namespace {

void close_fd(int &descriptor)
{
    if (descriptor >= 0) close(descriptor);
    descriptor = -1;
}

void drain_fd(int &descriptor, std::string &destination, std::size_t limit, bool &limit_exceeded)
{
    std::array<char, 8192> buffer{};
    while (descriptor >= 0) {
        const auto count = read(descriptor, buffer.data(), buffer.size());
        if (count > 0) {
            const auto amount        = static_cast<std::size_t>(count);
            const auto previous_size = destination.size();
            if (previous_size < limit) {
                const auto remaining = limit - destination.size();
                destination.append(buffer.data(), amount < remaining ? amount : remaining);
            }
            if (amount > limit - (previous_size < limit ? previous_size : limit)) limit_exceeded = true;
            continue;
        }
        if (count == 0) {
            close_fd(descriptor);
            return;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return;
        close_fd(descriptor);
    }
}

}  // namespace

ProcessResult PosixProcessRunner::run(const std::vector<std::string> &arguments, std::size_t output_limit)
{
    ProcessResult result;
    if (arguments.empty()) {
        result.error = "No command to execute";
        return result;
    }

    int output_pipe[2]{-1, -1};
    int error_pipe[2]{-1, -1};
    if (pipe(output_pipe) != 0 || pipe(error_pipe) != 0) {
        close_fd(output_pipe[0]);
        close_fd(output_pipe[1]);
        close_fd(error_pipe[0]);
        close_fd(error_pipe[1]);
        result.error = std::string("Unable to create process pipe: ") + std::strerror(errno);
        return result;
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addclose(&actions, output_pipe[0]);
    posix_spawn_file_actions_addclose(&actions, error_pipe[0]);
    posix_spawn_file_actions_adddup2(&actions, output_pipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, error_pipe[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, output_pipe[1]);
    posix_spawn_file_actions_addclose(&actions, error_pipe[1]);

    std::vector<char *> argv;
    argv.reserve(arguments.size() + 1);
    for (const auto &argument : arguments) argv.push_back(const_cast<char *>(argument.c_str()));
    argv.push_back(nullptr);

    pid_t pid              = -1;
    const auto spawn_error = posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close_fd(output_pipe[1]);
    close_fd(error_pipe[1]);
    if (spawn_error != 0) {
        close_fd(output_pipe[0]);
        close_fd(error_pipe[0]);
        result.error = std::string("Unable to start ") + arguments.front() + ": " + std::strerror(spawn_error);
        return result;
    }

    fcntl(output_pipe[0], F_SETFL, fcntl(output_pipe[0], F_GETFL) | O_NONBLOCK);
    fcntl(error_pipe[0], F_SETFL, fcntl(error_pipe[0], F_GETFL) | O_NONBLOCK);
    while (output_pipe[0] >= 0 || error_pipe[0] >= 0) {
        pollfd descriptors[2]{{output_pipe[0], POLLIN | POLLHUP, 0}, {error_pipe[0], POLLIN | POLLHUP, 0}};
        const auto polled = poll(descriptors, 2, 1000);
        if (polled < 0 && errno != EINTR) break;
        drain_fd(output_pipe[0], result.output, output_limit, result.output_limit_exceeded);
        drain_fd(error_pipe[0], result.error, output_limit, result.output_limit_exceeded);
        if (result.output_limit_exceeded && output_limit_policy_ == ProcessOutputLimit::terminate) {
            kill(pid, SIGKILL);
            break;
        }
    }
    close_fd(output_pipe[0]);
    close_fd(error_pipe[0]);

    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    if (WIFEXITED(status))
        result.exit_code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status))
        result.exit_code = 128 + WTERMSIG(status);
    return result;
}
