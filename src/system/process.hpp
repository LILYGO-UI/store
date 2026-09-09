#ifndef LILYGO_UI_STORE_PROCESS_HPP
#define LILYGO_UI_STORE_PROCESS_HPP

#include <cstddef>
#include <string>
#include <vector>

struct ProcessResult {
    int exit_code = -1;
    std::string output;
    std::string error;
    bool output_limit_exceeded = false;
};

class ProcessRunner {
public:
    virtual ~ProcessRunner()                                                        = default;
    [[nodiscard]] virtual ProcessResult run(const std::vector<std::string> &arguments,
                                            std::size_t output_limit = 1024 * 1024) = 0;
};

enum class ProcessOutputLimit { terminate, truncate };

class PosixProcessRunner final : public ProcessRunner {
public:
    explicit PosixProcessRunner(ProcessOutputLimit output_limit_policy = ProcessOutputLimit::terminate) noexcept
        : output_limit_policy_(output_limit_policy)
    {
    }

    [[nodiscard]] ProcessResult run(const std::vector<std::string> &arguments,
                                    std::size_t output_limit = 1024 * 1024) override;

private:
    ProcessOutputLimit output_limit_policy_;
};

#endif
