#include "app.h"

#include <iostream>
#include <string>
#include <vector>

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
    return has_suffix(path, ".264") || has_suffix(path, ".h264");
}

bool is_ts_output_path(const std::string &path) {
    return has_suffix(path, ".ts");
}

bool set_output_path(const std::string &path, RpiCamTsOptions &options) {
    if (is_h264_output_path(path)) {
        options.output_mode = OutputMode::RawH264;
    } else if (is_ts_output_path(path)) {
        options.output_mode = OutputMode::MpegTs;
    } else {
        return false;
    }

    options.output_path = path;
    options.output_path_explicit = true;
    return true;
}

void print_usage(const char *program_name) {
    std::cerr << "Usage:\n"
              << "  " << program_name
              << " [-v|--verbose] [output.264|output.h264|output.ts]\n"
              << "  " << program_name
              << " [-v|--verbose] [config.yml] "
                 "[output.264|output.h264|output.ts]\n"
              << "  " << program_name
              << " --generate-default-config <output.yml>\n";
}

} // namespace

int main(int argc, char **argv) {
    RpiCamTsOptions options;
    std::vector<std::string> arguments;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "-v" || argument == "--verbose") {
            options.verbose = true;
        } else {
            arguments.push_back(argument);
        }
    }

    if (arguments.empty()) {
        // Use the default option values.
    } else if (arguments.size() == 1) {
        const std::string &argument = arguments[0];
        if (is_config_path(argument)) {
            options.config_path = argument;
            options.config_path_explicit = true;
        } else if (!set_output_path(argument, options)) {
            print_usage(argv[0]);
            return 1;
        }
    } else if (arguments.size() == 2 &&
               arguments[0] == "--generate-default-config" &&
               is_config_path(arguments[1])) {
        options.generate_default_config = true;
        options.generate_default_config_path = arguments[1];
    } else if (arguments.size() == 2 && is_config_path(arguments[0]) &&
               set_output_path(arguments[1], options)) {
        options.config_path = arguments[0];
        options.config_path_explicit = true;
    } else {
        print_usage(argv[0]);
        return 1;
    }

    RpiCamTsApp app(options);
    return app.run();
}
