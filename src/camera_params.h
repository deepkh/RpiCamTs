#pragma once

#include <map>
#include <string>

std::string build_camera_parameters(
    const std::map<std::string, std::string> &config_values = {});
