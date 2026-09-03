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
  virtual ~ProcessRunner() = default;
  [[nodiscard]] virtual ProcessResult
  run(const std::vector<std::string> &arguments,
      std::size_t output_limit = 1024 * 1024) = 0;
};

class PosixProcessRunner final : public ProcessRunner {
public:
  [[nodiscard]] ProcessResult run(const std::vector<std::string> &arguments,
                                  std::size_t output_limit = 1024 *
                                                             1024) override;
};

#endif
