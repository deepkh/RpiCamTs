#pragma once

#include <string>

bool file_exists(const std::string &path);
bool is_executable(const std::string &path);
std::string absolute_path(const std::string &path);
std::string default_mtxrpicam_path();
void chdir_to_project_root();
