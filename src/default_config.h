#pragma once

#include <string>

std::string default_config_yaml();
bool write_default_config_file(const std::string &path,
                               std::string &error_message);
