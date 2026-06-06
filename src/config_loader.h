#pragma once

#include <map>
#include <string>

struct ConfigLoadResult {
    bool loaded = false;
    std::string path;
    std::map<std::string, std::string> values;
};

ConfigLoadResult load_config_file(const std::string &path);
bool config_file_exists(const std::string &path);
