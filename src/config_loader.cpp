#include "config_loader.h"

#include <cctype>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {

std::string trim(const std::string &value) {
    std::size_t begin = 0;
    while (begin < value.size() &&
           std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }

    std::size_t end = value.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(begin, end - begin);
}

std::string strip_comment(const std::string &line) {
    char quote = '\0';
    for (std::size_t index = 0; index < line.size(); ++index) {
        const char character = line[index];
        if ((character == '\'' || character == '"') &&
            (index == 0 || line[index - 1] != '\\')) {
            if (quote == '\0') {
                quote = character;
            } else if (quote == character) {
                quote = '\0';
            }
        } else if (character == '#' && quote == '\0') {
            return line.substr(0, index);
        }
    }
    return line;
}

std::string unquote(const std::string &value, const std::string &path,
                    std::size_t line_number) {
    if (value.empty()) {
        return value;
    }

    const bool starts_quoted = value.front() == '\'' || value.front() == '"';
    const bool ends_quoted = value.back() == '\'' || value.back() == '"';
    if (starts_quoted || ends_quoted) {
        if (value.size() < 2 || value.front() != value.back()) {
            throw std::runtime_error(path + ":" +
                                     std::to_string(line_number) +
                                     ": unmatched value quote");
        }
        return value.substr(1, value.size() - 2);
    }
    return value;
}

} // namespace

ConfigLoadResult load_config_file(const std::string &path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("cannot open config file: " + path);
    }

    ConfigLoadResult result;
    result.loaded = true;
    result.path = path;

    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        line = trim(strip_comment(line));
        if (line.empty()) {
            continue;
        }

        const std::size_t separator = line.find(':');
        if (separator == std::string::npos) {
            throw std::runtime_error(path + ":" +
                                     std::to_string(line_number) +
                                     ": expected Key: Value");
        }

        const std::string key = trim(line.substr(0, separator));
        const std::string value = trim(line.substr(separator + 1));
        if (key.empty()) {
            throw std::runtime_error(path + ":" +
                                     std::to_string(line_number) +
                                     ": config key is empty");
        }
        result.values[key] = unquote(value, path, line_number);
    }

    if (!input.eof()) {
        throw std::runtime_error("error reading config file: " + path);
    }
    return result;
}

bool config_file_exists(const std::string &path) {
    std::ifstream input(path);
    return input.good();
}
