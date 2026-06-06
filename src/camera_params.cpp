#include "camera_params.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>

#include <unistd.h>

namespace {

std::string default_tuning_file() {
    const char *environment_value = std::getenv("MTXRPICAM_TUNING_FILE");
    if (environment_value != nullptr && environment_value[0] != '\0') {
        return environment_value;
    }

    const std::array<const char *, 5> candidates = {
        "./third_party/mediamtx-rpicamera-fork/share/libcamera/ipa/rpi/pisp/"
        "imx708_wide.json",
        "./share/libcamera/ipa/rpi/pisp/imx708_wide.json",
        "/usr/share/libcamera/ipa/rpi/pisp/imx708_wide.json",
        "./build/mediamtx-rpicamera-fork/install/share/libcamera/ipa/rpi/pisp/"
        "imx708_wide.json",
        "./third_party/mediamtx-rpicamera-fork/subprojects/libcamera/src/ipa/"
        "rpi/pisp/data/imx708_wide.json",
    };

    for (const char *candidate : candidates) {
        if (access(candidate, R_OK) == 0) {
            return candidate;
        }
    }
    return candidates[2];
}

std::string base64_encode(const std::string &source) {
    static constexpr char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    if (source.empty()) {
        return "AA==";
    }

    std::string output;
    output.reserve(4 * ((source.size() + 2) / 3));

    for (std::size_t offset = 0; offset < source.size(); offset += 3) {
        const std::uint32_t a = static_cast<unsigned char>(source[offset]);
        const std::uint32_t b =
            offset + 1 < source.size()
                ? static_cast<unsigned char>(source[offset + 1])
                : 0;
        const std::uint32_t c =
            offset + 2 < source.size()
                ? static_cast<unsigned char>(source[offset + 2])
                : 0;
        const std::uint32_t value = (a << 16U) | (b << 8U) | c;

        output.push_back(table[(value >> 18U) & 0x3fU]);
        output.push_back(table[(value >> 12U) & 0x3fU]);
        output.push_back(offset + 1 < source.size()
                             ? table[(value >> 6U) & 0x3fU]
                             : '=');
        output.push_back(offset + 2 < source.size() ? table[value & 0x3fU]
                                                    : '=');
    }
    return output;
}

std::string parameter_value(
    const std::map<std::string, std::string> &config_values, const char *key,
    const std::string &fallback) {
    const auto config_value = config_values.find(key);
    if (config_value != config_values.end()) {
        return config_value->second;
    }

    const char *value = std::getenv(key);
    return value != nullptr ? value : fallback;
}

void append_parameter(std::string &parameters, const char *key,
                      const std::string &value, bool encode) {
    if (!parameters.empty()) {
        parameters.push_back(' ');
    }
    parameters += key;
    parameters.push_back(':');
    parameters += encode ? base64_encode(value) : value;
}

} // namespace

std::string build_camera_parameters(
    const std::map<std::string, std::string> &config_values) {
    std::string parameters;
    parameters.reserve(1024);

    const auto raw = [&parameters, &config_values](const char *key,
                                                   const char *fallback) {
        append_parameter(parameters, key,
                         parameter_value(config_values, key, fallback), false);
    };
    const auto encoded = [&parameters, &config_values](const char *key,
                                                       const char *fallback) {
        append_parameter(parameters, key,
                         parameter_value(config_values, key, fallback), true);
    };

    encoded("LogLevel", "info");
    raw("CameraID", "0");
    raw("Width", "2304");
    raw("Height", "1296");
    raw("HFlip", "1");
    raw("VFlip", "0");
    raw("Brightness", "0.0");
    raw("Contrast", "1.0");
    raw("Saturation", "1.0");
    raw("Sharpness", "1.0");
    encoded("Exposure", "short");
    encoded("AWB", "auto");
    raw("AWBGainRed", "0.0");
    raw("AWBGainBlue", "0.0");
    encoded("Denoise", "cdn_hq");
    raw("Shutter", "0");
    encoded("Metering", "centre");
    raw("Gain", "0.0");
    raw("EV", "0.0");
    encoded("ROI", "");
    raw("HDR", "0");
    std::string tuning_file =
        parameter_value(config_values, "TuningFile", default_tuning_file());
    if (tuning_file.empty()) {
        tuning_file = default_tuning_file();
    }
    append_parameter(parameters, "TuningFile", tuning_file, true);
    encoded("Mode", "");
    raw("MinFPS", "5.0");
    raw("MaxFPS", "60.0");
    encoded("AfMode", "manual");
    encoded("AfRange", "normal");
    encoded("AfSpeed", "normal");
    raw("LensPosition", "0.0");
    encoded("AfWindow", "");
    raw("FlickerPeriod", "0");
    raw("TextOverlayEnable", "0");
    encoded("TextOverlay", "%Y-%m-%d %H:%M:%S - MediaMTX");
    encoded("Codec", "auto");
    raw("IDRPeriod", "60");
    raw("Bitrate", "10000000");
    encoded("HardwareH264Profile", "main");
    encoded("HardwareH264Level", "4.1");
    encoded("SoftwareH264Profile", "baseline");
    encoded("SoftwareH264Level", "4.1");
    raw("SecondaryWidth", "0");
    raw("SecondaryHeight", "0");
    raw("SecondaryFPS", "0.0");
    raw("SecondaryMJPEGQuality", "0");

    return parameters;
}
