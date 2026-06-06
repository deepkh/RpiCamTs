#include "path_utils.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

#include <unistd.h>

namespace {

std::string parent_directory(const std::string &path) {
    const std::size_t slash = path.find_last_of('/');
    if (slash == std::string::npos) {
        return ".";
    }
    if (slash == 0) {
        return "/";
    }
    return path.substr(0, slash);
}

bool is_project_root(const std::string &path) {
    const std::string marker =
        path + "/third_party/mediamtx-rpicamera-fork/fake_pipe_reader.c";
    return access(marker.c_str(), R_OK) == 0;
}

} // namespace

bool file_exists(const std::string &path) {
    return access(path.c_str(), F_OK) == 0;
}

bool is_executable(const std::string &path) {
    return access(path.c_str(), X_OK) == 0;
}

std::string absolute_path(const std::string &path) {
    char *resolved = realpath(path.c_str(), nullptr);
    if (resolved == nullptr) {
        return {};
    }

    std::string result(resolved);
    std::free(resolved);
    return result;
}

std::string absolute_path_allow_missing(const std::string &path) {
    std::error_code error;
    const std::filesystem::path absolute = std::filesystem::absolute(path, error);
    if (error) {
        return {};
    }
    return absolute.lexically_normal().string();
}

std::string executable_directory() {
    const std::string executable = absolute_path("/proc/self/exe");
    if (executable.empty()) {
        return {};
    }
    return parent_directory(executable);
}

void chdir_to_project_root() {
    const std::string executable_dir = executable_directory();
    if (executable_dir.empty()) {
        return;
    }
    const std::array<std::string, 2> candidates = {
        executable_dir,
        parent_directory(executable_dir),
    };

    for (const std::string &candidate : candidates) {
        if (is_project_root(candidate)) {
            if (chdir(candidate.c_str()) != 0) {
                std::perror("chdir project root");
            }
            return;
        }
    }

    if (chdir(executable_dir.c_str()) != 0) {
        std::perror("chdir executable directory");
    }
}

std::string default_mtxrpicam_path() {
    const std::array<const char *, 6> candidates = {
        "./third_party/mediamtx-rpicamera-fork/mtxrpicam",
        "./third_party/mediamtx-rpicamera-fork/build/mtxrpicam",
        "./mtxrpicam",
        "./build/mtxrpicam",
        "./build/mediamtx-rpicamera-fork/mtxrpicam",
        "./build/mediamtx-rpicamera-fork/install/bin/mtxrpicam",
    };

    for (const char *candidate : candidates) {
        if (is_executable(candidate)) {
            return candidate;
        }
    }
    return {};
}
