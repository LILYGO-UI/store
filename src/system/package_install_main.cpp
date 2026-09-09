#include "system/apt_installer.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <iostream>
#include <unistd.h>

extern char **environ;

int main(int argc, char **argv)
{
    if (argc != 5) {
        std::cerr << "Usage: " << std::filesystem::path(argv[0]).filename().string()
                  << " <package> <version> <sha256> <absolute-deb-path>\n";
        return 2;
    }
    if (geteuid() != 0) {
        std::cerr << "Package installation requires administrator privileges\n";
        return 1;
    }

    // Do not let the invoking user's APT_CONFIG, DPKG_ROOT, or PATH alter a
    // privileged transaction. APT still reads the device's system configuration.
    char *empty_environment[] = {nullptr};
    environ                   = empty_environment;
    if (setenv("PATH", "/usr/sbin:/usr/bin:/sbin:/bin", 1) != 0 || setenv("LC_ALL", "C", 1) != 0 ||
        setenv("DEBIAN_FRONTEND", "noninteractive", 1) != 0) {
        std::cerr << "Unable to initialize package installer environment\n";
        return 1;
    }
    const int input = open("/dev/null", O_RDONLY);
    if (input < 0 || dup2(input, STDIN_FILENO) < 0) {
        std::cerr << "Unable to initialize package installer input: " << std::strerror(errno) << '\n';
        if (input >= 0) close(input);
        return 1;
    }
    if (input != STDIN_FILENO) close(input);

    try {
        PosixProcessRunner runner(ProcessOutputLimit::truncate);
        const auto result = install_debian_package(runner, {argv[1], argv[2], argv[3], argv[4]});
        if (!result) {
            std::cerr << result.error << '\n';
            return 1;
        }
        std::cout << (result.launcher_update_queued ? "Launcher update queued\n" : "Package installation completed\n");
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Package installation failed: " << error.what() << '\n';
        return 1;
    }
}
