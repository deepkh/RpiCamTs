#include "mtxrpicam_process.h"

#include "path_utils.h"

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

void prepend_environment_path(const char *key, const std::string &path) {
    const char *old_path = std::getenv(key);
    std::string new_path = path;
    if (old_path != nullptr && old_path[0] != '\0') {
        new_path += ':';
        new_path += old_path;
    }
    setenv(key, new_path.c_str(), 1);
}

void set_libpisp_config_file(const std::string &runtime_directory) {
    const std::string bundled_config =
        runtime_directory + "/share/libpisp/backend_default_config.json";
    if (!runtime_directory.empty() &&
        access(bundled_config.c_str(), R_OK) == 0) {
        setenv("LIBPISP_BE_CONFIG_FILE", bundled_config.c_str(), 1);
        return;
    }

    const std::array<const char *, 4> candidates = {
        "./third_party/mediamtx-rpicamera-fork/build/subprojects/libpisp/src/"
        "libpisp/backend/backend_default_config.json",
        "./third_party/mediamtx-rpicamera-fork/subprojects/libpisp/src/"
        "libpisp/backend/backend_default_config.json",
        "./build/mediamtx-rpicamera-fork/subprojects/libpisp/src/libpisp/"
        "backend/backend_default_config.json",
        "./build/mediamtx-rpicamera-fork/install/share/libpisp/"
        "backend_default_config.json",
    };

    for (const char *candidate : candidates) {
        if (access(candidate, R_OK) != 0) {
            continue;
        }

        const std::string resolved = absolute_path(candidate);
        if (!resolved.empty()) {
            setenv("LIBPISP_BE_CONFIG_FILE", resolved.c_str(), 1);
        }
        return;
    }
}

void configure_child_environment(int config_fd, int video_fd) {
    const std::string config_fd_value = std::to_string(config_fd);
    const std::string video_fd_value = std::to_string(video_fd);
    setenv("PIPE_CONF_FD", config_fd_value.c_str(), 1);
    setenv("PIPE_VIDEO_FD", video_fd_value.c_str(), 1);

    const std::array<const char *, 7> library_paths = {
        "./third_party/mediamtx-rpicamera-fork/subprojects/libcamera/src/"
        "libcamera/base",
        "./third_party/mediamtx-rpicamera-fork/subprojects/libcamera/src/"
        "libcamera",
        "./third_party/mediamtx-rpicamera-fork/build/subprojects/libcamera/src/"
        "libcamera/base",
        "./third_party/mediamtx-rpicamera-fork/build/subprojects/libcamera/src/"
        "libcamera",
        "./build/mediamtx-rpicamera-fork/subprojects/libcamera/src/libcamera/"
        "base",
        "./build/mediamtx-rpicamera-fork/subprojects/libcamera/src/libcamera",
        "./dst",
    };
    for (const char *path : library_paths) {
        prepend_environment_path("LD_LIBRARY_PATH", path);
    }

    const std::string runtime_directory = executable_directory();
    if (!runtime_directory.empty()) {
        prepend_environment_path("LD_LIBRARY_PATH", runtime_directory);
    }

    const std::array<const char *, 3> ipa_module_paths = {
        "./build/mediamtx-rpicamera-fork/install/lib/aarch64-linux-gnu/"
        "libcamera",
        "./build/mediamtx-rpicamera-fork/subprojects/libcamera/src/ipa/rpi/"
        "pisp",
        "./build/mediamtx-rpicamera-fork/subprojects/libcamera/src/ipa/rpi/"
        "vc4",
    };
    for (const char *path : ipa_module_paths) {
        if (access(path, R_OK) == 0) {
            prepend_environment_path("LIBCAMERA_IPA_MODULE_PATH", path);
        }
    }

    if (!runtime_directory.empty()) {
        const std::string bundled_ipa_modules =
            runtime_directory + "/libcamera";
        if (access(bundled_ipa_modules.c_str(), R_OK) == 0) {
            prepend_environment_path("LIBCAMERA_IPA_MODULE_PATH",
                                     bundled_ipa_modules);
        }
    }

    const char *ipa_config_path =
        "./build/mediamtx-rpicamera-fork/install/share/libcamera/ipa";
    if (access(ipa_config_path, R_OK) == 0) {
        prepend_environment_path("LIBCAMERA_IPA_CONFIG_PATH", ipa_config_path);
    }
    if (!runtime_directory.empty()) {
        const std::string bundled_ipa_config =
            runtime_directory + "/share/libcamera/ipa";
        if (access(bundled_ipa_config.c_str(), R_OK) == 0) {
            prepend_environment_path("LIBCAMERA_IPA_CONFIG_PATH",
                                     bundled_ipa_config);
        }
    }
    set_libpisp_config_file(runtime_directory);
}

} // namespace

MtxRpiCamProcess::MtxRpiCamProcess(MtxRpiCamProcessOptions options)
    : options_(std::move(options)) {}

bool MtxRpiCamProcess::start() {
    pid_ = fork();
    if (pid_ < 0) {
        return false;
    }

    if (pid_ == 0) {
        close(options_.conf_write_fd);
        close(options_.video_read_fd);
        signal(SIGINT, SIG_IGN);
        signal(SIGTERM, SIG_IGN);
        configure_child_environment(options_.conf_read_fd,
                                    options_.video_write_fd);
        execl(options_.mtxrpicam_path.c_str(),
              options_.mtxrpicam_path.c_str(), static_cast<char *>(nullptr));
        std::perror("exec mtxrpicam");
        _exit(127);
    }

    return true;
}

void MtxRpiCamProcess::request_stop() {
    if (pid_ > 0) {
        kill(pid_, SIGTERM);
    }
}

int MtxRpiCamProcess::wait() {
    if (pid_ <= 0) {
        return 1;
    }

    int status = 0;
    while (waitpid(pid_, &status, 0) < 0) {
        if (errno == EINTR) {
            continue;
        }
        std::perror("waitpid");
        return 1;
    }
    pid_ = -1;

    if (WIFEXITED(status)) {
        const int exit_code = WEXITSTATUS(status);
        if (exit_code != 0) {
            std::cerr << "[RpiCamTs] mtxrpicam exited with status " << exit_code
                      << '\n';
        }
        return exit_code;
    }
    if (WIFSIGNALED(status)) {
        std::cerr << "[RpiCamTs] mtxrpicam terminated by signal "
                  << WTERMSIG(status) << '\n';
        return 1;
    }
    return 1;
}

pid_t MtxRpiCamProcess::pid() const { return pid_; }
