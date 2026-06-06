#pragma once

#include <map>
#include <string>

std::string build_camera_parameters(
    const std::map<std::string, std::string> &config_values = {});

int get_camera_param_int(
    const std::map<std::string, std::string> &config_values,
    const std::string &key, int default_value);
