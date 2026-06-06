#include "app.h"

#include <iostream>
#include <string>

namespace {

void print_usage(const char *program_name) {
    std::cerr << "Usage:\n"
              << "  " << program_name << " [config.yml]\n"
              << "  " << program_name
              << " --generate-default-config <output.yml>\n";
}

} // namespace

int main(int argc, char **argv) {
    RpiCamTsOptions options;
    if (argc == 1) {
        // Use the default option values.
    } else if (argc == 2 && std::string(argv[1]).rfind("--", 0) != 0) {
        options.config_path = argv[1];
        options.config_path_explicit = true;
    } else if (argc == 3 &&
               std::string(argv[1]) == "--generate-default-config") {
        options.generate_default_config = true;
        options.generate_default_config_path = argv[2];
    } else {
        print_usage(argv[0]);
        return 1;
    }

    RpiCamTsApp app(options);
    return app.run();
}
