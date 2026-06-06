#include "app.h"

#include <iostream>

int main(int argc, char **argv) {
    if (argc > 2) {
        std::cerr << "Usage: " << argv[0] << " [/path/to/mtxrpicam]\n";
        return 1;
    }

    RpiCamTsOptions options;
    if (argc == 2) {
        options.mtxrpicam_path = argv[1];
    }

    RpiCamTsApp app(options);
    return app.run();
}
