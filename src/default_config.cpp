#include "default_config.h"

#include <cerrno>
#include <cstring>
#include <string>

#include <fcntl.h>
#include <unistd.h>

std::string default_config_yaml() {
    return R"(# RpiCamTs default configuration
#
# Generate this file with:
#
#   ./dst/RpiCamTs --generate-default-config RpiCamTs.yml
#
# Run with:
#
#   ./dst/RpiCamTs RpiCamTs.yml
#
# Or if the file is named RpiCamTs.yml:
#
#   ./dst/RpiCamTs

MtxRpiCamPath: ""

LogLevel: info
CameraID: 0

Width: 2304
Height: 1296

HFlip: 0
VFlip: 0

Brightness: 0
Contrast: 1
Saturation: 1
Sharpness: 1

Exposure: normal
AWB: auto
AWBGainRed: 0
AWBGainBlue: 0

Denoise: auto
Shutter: 0
Metering: centre
Gain: 0
EV: 0
ROI: "0,0,0,0"
HDR: off

TuningFile: ""

Mode: ""

MinFPS: 5.0
MaxFPS: 60.0

AfMode: continuous
AfRange: normal
AfSpeed: normal
LensPosition: 0
AfWindow: "0,0,0,0"
FlickerPeriod: 0

TextOverlayEnable: 0
TextOverlay: ""

Codec: auto
IDRPeriod: 60
Bitrate: 10000000

HardwareH264Profile: main
HardwareH264Level: "4.1"

SoftwareH264Profile: baseline
SoftwareH264Level: "4.1"

SecondaryWidth: 0
SecondaryHeight: 0
SecondaryFPS: 0
SecondaryMJPEGQuality: 60
)";
}

bool write_default_config_file(const std::string &path,
                               std::string &error_message) {
    error_message.clear();
    if (path.empty()) {
        error_message = "Default config output path is empty.";
        return false;
    }

    const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) {
        if (errno == EEXIST) {
            error_message = "Config file already exists: " + path +
                            "\n[RpiCamTs] Remove it first if you want to "
                            "regenerate it.";
        } else {
            error_message = "Failed to create default config '" + path +
                            "': " + std::strerror(errno);
        }
        return false;
    }

    const std::string content = default_config_yaml();
    std::size_t written = 0;
    while (written < content.size()) {
        const ssize_t result =
            write(fd, content.data() + written, content.size() - written);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            const int write_error = errno;
            close(fd);
            unlink(path.c_str());
            error_message = "Failed to write default config '" + path +
                            "': " + std::strerror(write_error);
            return false;
        }
        written += static_cast<std::size_t>(result);
    }

    if (close(fd) != 0) {
        const int close_error = errno;
        unlink(path.c_str());
        error_message = "Failed to finish default config '" + path +
                        "': " + std::strerror(close_error);
        return false;
    }

    return true;
}
