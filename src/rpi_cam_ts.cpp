#include "app.h"

#include <iostream>
#include <string>

namespace {

bool has_suffix(const std::string &value, const std::string &suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) ==
               0;
}

bool is_config_path(const std::string &path) {
    return has_suffix(path, ".yml") || has_suffix(path, ".yaml");
}

bool is_h264_output_path(const std::string &path) {
    return has_suffix(path, ".264");
}

void print_usage(const char *program_name) {
    std::cerr << "Usage:\n"
              << "  " << program_name << " [output.264]\n"
              << "  " << program_name << " [config.yml] [output.264]\n"
              << "  " << program_name
              << " --generate-default-config <output.yml>\n";
}

} // namespace

int main(int argc, char **argv) {
    RpiCamTsOptions options;
    if (argc == 1) {
        // Use the default option values.
    } else if (argc == 2) {
        const std::string argument = argv[1];
        if (is_config_path(argument)) {
            options.config_path = argument;
            options.config_path_explicit = true;
        } else if (is_h264_output_path(argument)) {
            options.h264_output_path = argument;
            options.h264_output_path_explicit = true;
        } else {
            print_usage(argv[0]);
            return 1;
        }
    } else if (argc == 3 &&
               std::string(argv[1]) == "--generate-default-config" &&
               is_config_path(argv[2])) {
        options.generate_default_config = true;
        options.generate_default_config_path = argv[2];
    } else if (argc == 3 && is_config_path(argv[1]) &&
               is_h264_output_path(argv[2])) {
        options.config_path = argv[1];
        options.config_path_explicit = true;
        options.h264_output_path = argv[2];
        options.h264_output_path_explicit = true;
    } else {
        print_usage(argv[0]);
        return 1;
    }

    RpiCamTsApp app(options);
    return app.run();
}
