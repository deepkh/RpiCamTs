#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

volatile sig_atomic_t g_stop_requested = 0;

constexpr std::size_t kMaximumPacketSize = 256U * 1024U * 1024U;

void on_signal(int signal_number) {
    (void)signal_number;
    g_stop_requested = 1;
}

bool install_signal_handlers() {
    struct sigaction stop_action {};
    stop_action.sa_handler = on_signal;
    sigemptyset(&stop_action.sa_mask);

    struct sigaction ignore_action {};
    ignore_action.sa_handler = SIG_IGN;
    sigemptyset(&ignore_action.sa_mask);

    return sigaction(SIGINT, &stop_action, nullptr) == 0 &&
           sigaction(SIGTERM, &stop_action, nullptr) == 0 &&
           sigaction(SIGPIPE, &ignore_action, nullptr) == 0;
}

std::string real_path(const std::string &path) {
    char *resolved = realpath(path.c_str(), nullptr);
    if (resolved == nullptr) {
        return {};
    }

    std::string result(resolved);
    std::free(resolved);
    return result;
}

std::string parent_directory(const std::string &path) {
    const std::size_t slash = path.find_last_of('/');
    if (slash == std::string::npos) {
        return ".";
    }
    if (slash == 0) {
        return "/";
    }
    return path.substr(0, slash);
}

bool is_project_root(const std::string &path) {
    const std::string marker =
        path + "/third_party/mediamtx-rpicamera-fork/fake_pipe_reader.c";
    return access(marker.c_str(), R_OK) == 0;
}

void chdir_to_project_root(const char *argv0) {
    std::string executable = real_path("/proc/self/exe");
    if (executable.empty() && argv0 != nullptr) {
        executable = real_path(argv0);
    }
    if (executable.empty()) {
        return;
    }

    const std::string executable_directory = parent_directory(executable);
    const std::array<std::string, 2> candidates = {
        executable_directory,
        parent_directory(executable_directory),
    };

    for (const std::string &candidate : candidates) {
        if (is_project_root(candidate)) {
            if (chdir(candidate.c_str()) != 0) {
                std::perror("chdir project root");
            }
            return;
        }
    }

    if (chdir(executable_directory.c_str()) != 0) {
        std::perror("chdir executable directory");
    }
}

std::string default_mtxrpicam_path() {
    const std::array<const char *, 6> candidates = {
        "./third_party/mediamtx-rpicamera-fork/mtxrpicam",
        "./third_party/mediamtx-rpicamera-fork/build/mtxrpicam",
        "./mtxrpicam",
        "./build/mtxrpicam",
        "./build/mediamtx-rpicamera-fork/mtxrpicam",
        "./build/mediamtx-rpicamera-fork/install/bin/mtxrpicam",
    };

    for (const char *candidate : candidates) {
        if (access(candidate, X_OK) == 0) {
            return candidate;
        }
    }
    return {};
}

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

void prepend_environment_path(const char *key, const std::string &path) {
    const char *old_path = std::getenv(key);
    std::string new_path = path;
    if (old_path != nullptr && old_path[0] != '\0') {
        new_path += ":";
        new_path += old_path;
    }
    setenv(key, new_path.c_str(), 1);
}

void prepend_ld_library_path(const std::string &path) {
    prepend_environment_path("LD_LIBRARY_PATH", path);
}

void set_libpisp_config_file() {
    const std::array<const char *, 4> candidates = {
        "./third_party/mediamtx-rpicamera-fork/build/subprojects/libpisp/src/"
        "libpisp/backend/backend_default_config.json",
        "./third_party/mediamtx-rpicamera-fork/subprojects/libpisp/src/"
        "libpisp/backend/backend_default_config.json",
        "./build/mediamtx-rpicamera-fork/subprojects/libpisp/src/libpisp/"
        "backend/backend_default_config.json",
        "./build/mediamtx-rpicamera-fork/install/share/libpisp/"
        "backend_default_config.json",
    };

    for (const char *candidate : candidates) {
        if (access(candidate, R_OK) != 0) {
            continue;
        }

        const std::string resolved = real_path(candidate);
        if (!resolved.empty()) {
            setenv("LIBPISP_BE_CONFIG_FILE", resolved.c_str(), 1);
        }
        return;
    }
}

std::string base64_encode(const std::string &source) {
    static constexpr char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    if (source.empty()) {
        // Keep compatibility with the reference implementation's empty value.
        return "AA==";
    }

    std::string output;
    output.reserve(4 * ((source.size() + 2) / 3));

    for (std::size_t offset = 0; offset < source.size(); offset += 3) {
        const std::uint32_t a =
            static_cast<unsigned char>(source[offset]);
        const std::uint32_t b = offset + 1 < source.size()
                                    ? static_cast<unsigned char>(source[offset + 1])
                                    : 0;
        const std::uint32_t c = offset + 2 < source.size()
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

std::string environment_value(const char *key, const std::string &fallback) {
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

std::string build_parameters() {
    std::string parameters;
    parameters.reserve(1024);

    const auto raw = [&parameters](const char *key, const char *fallback) {
        append_parameter(parameters, key, environment_value(key, fallback),
                         false);
    };
    const auto encoded = [&parameters](const char *key, const char *fallback) {
        append_parameter(parameters, key, environment_value(key, fallback),
                         true);
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
    append_parameter(parameters, "TuningFile",
                     environment_value("TuningFile", default_tuning_file()),
                     true);
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

bool write_full(int fd, const void *buffer, std::size_t size) {
    const auto *data = static_cast<const std::uint8_t *>(buffer);
    while (size > 0) {
        const ssize_t written = write(fd, data, size);
        if (written < 0) {
            if (errno == EINTR && !g_stop_requested) {
                continue;
            }
            return false;
        }
        if (written == 0) {
            errno = EIO;
            return false;
        }
        data += written;
        size -= static_cast<std::size_t>(written);
    }
    return true;
}

bool write_packet(int fd, char kind, const std::string &payload) {
    if (payload.size() >
        std::numeric_limits<std::uint32_t>::max() - sizeof(kind)) {
        errno = EOVERFLOW;
        return false;
    }

    const std::uint32_t size =
        static_cast<std::uint32_t>(sizeof(kind) + payload.size());
    return write_full(fd, &size, sizeof(size)) &&
           write_full(fd, &kind, sizeof(kind)) &&
           write_full(fd, payload.data(), payload.size());
}

enum class ReadResult {
    Ok,
    EndOfFile,
    Stopped,
    Error,
};

ReadResult read_full(int fd, void *buffer, std::size_t size) {
    auto *data = static_cast<std::uint8_t *>(buffer);
    while (size > 0) {
        const ssize_t count = read(fd, data, size);
        if (count < 0) {
            if (errno == EINTR) {
                if (g_stop_requested) {
                    return ReadResult::Stopped;
                }
                continue;
            }
            return ReadResult::Error;
        }
        if (count == 0) {
            return ReadResult::EndOfFile;
        }
        data += count;
        size -= static_cast<std::size_t>(count);
    }
    return ReadResult::Ok;
}

class BitReader {
  public:
    BitReader(const std::uint8_t *data, std::size_t size)
        : data_(data), size_(size) {}

    bool read_unsigned_exp_golomb(std::uint32_t &value) {
        std::uint32_t leading_zero_bits = 0;
        bool bit = false;
        while (true) {
            if (!read_bit(bit)) {
                return false;
            }
            if (bit) {
                break;
            }
            if (++leading_zero_bits >= 32) {
                return false;
            }
        }

        value = (1U << leading_zero_bits) - 1U;
        for (std::uint32_t index = 0; index < leading_zero_bits; ++index) {
            if (!read_bit(bit)) {
                return false;
            }
            if (bit) {
                value += 1U << (leading_zero_bits - index - 1U);
            }
        }
        return true;
    }

  private:
    bool read_bit(bool &bit) {
        if (bit_offset_ >= size_ * 8U) {
            return false;
        }
        const std::size_t byte_offset = bit_offset_ / 8U;
        const std::size_t bit_in_byte = 7U - (bit_offset_ % 8U);
        bit = ((data_[byte_offset] >> bit_in_byte) & 0x01U) != 0;
        ++bit_offset_;
        return true;
    }

    const std::uint8_t *data_;
    std::size_t size_;
    std::size_t bit_offset_ = 0;
};

std::size_t h264_start_code_size(const std::uint8_t *buffer,
                                 std::size_t size, std::size_t offset) {
    if (offset + 3 <= size && buffer[offset] == 0x00 &&
        buffer[offset + 1] == 0x00 && buffer[offset + 2] == 0x01) {
        return 3;
    }
    if (offset + 4 <= size && buffer[offset] == 0x00 &&
        buffer[offset + 1] == 0x00 && buffer[offset + 2] == 0x00 &&
        buffer[offset + 3] == 0x01) {
        return 4;
    }
    return 0;
}

std::vector<std::uint8_t> h264_copy_rbsp(const std::uint8_t *payload,
                                         std::size_t size) {
    std::vector<std::uint8_t> rbsp;
    rbsp.reserve(std::min<std::size_t>(size, 64));
    unsigned int zero_count = 0;

    for (std::size_t index = 0; index < size && rbsp.size() < 64; ++index) {
        if (zero_count >= 2 && payload[index] == 0x03) {
            zero_count = 0;
            continue;
        }

        rbsp.push_back(payload[index]);
        if (payload[index] == 0x00) {
            ++zero_count;
        } else {
            zero_count = 0;
        }
    }
    return rbsp;
}

std::string h264_slice_type_name(const std::uint8_t *payload,
                                 std::size_t size) {
    const std::vector<std::uint8_t> rbsp = h264_copy_rbsp(payload, size);
    BitReader reader(rbsp.data(), rbsp.size());
    std::uint32_t first_macroblock = 0;
    std::uint32_t slice_type = 0;
    if (!reader.read_unsigned_exp_golomb(first_macroblock) ||
        !reader.read_unsigned_exp_golomb(slice_type)) {
        return {};
    }

    static const std::array<const char *, 5> names = {"P", "B", "I", "SP",
                                                       "SI"};
    return names[slice_type % names.size()];
}

std::string collect_h264_nalu_info(const std::uint8_t *buffer,
                                   std::size_t size) {
    std::string information;
    const auto append = [&information](const std::string &value) {
        if (value.empty()) {
            return;
        }
        if (!information.empty()) {
            information.push_back(' ');
        }
        information += value;
    };

    std::size_t offset = 0;
    while (offset < size) {
        const std::size_t start_code_size =
            h264_start_code_size(buffer, size, offset);
        if (start_code_size == 0) {
            ++offset;
            continue;
        }

        const std::size_t header_offset = offset + start_code_size;
        if (header_offset >= size) {
            break;
        }

        std::size_t next_start_code = header_offset + 1;
        while (next_start_code < size &&
               h264_start_code_size(buffer, size, next_start_code) == 0) {
            ++next_start_code;
        }

        const std::uint8_t type = buffer[header_offset] & 0x1fU;
        switch (type) {
        case 6:
            append("SEI");
            break;
        case 7:
            append("SPS");
            break;
        case 8:
            append("PPS");
            break;
        case 9:
            append("AUD");
            break;
        default:
            break;
        }

        if ((type == 1 || type == 5) && header_offset + 1 <= next_start_code) {
            append(h264_slice_type_name(
                buffer + header_offset + 1,
                next_start_code - (header_offset + 1)));
        }

        offset = next_start_code;
    }
    return information;
}

struct VideoPacket {
    char kind = '\0';
    std::uint64_t timestamp = 0;
    std::size_t payload_size = 0;
    std::vector<std::uint8_t> payload;
};

ReadResult read_video_packet(int fd, VideoPacket &packet) {
    std::uint32_t packet_size = 0;
    ReadResult result = read_full(fd, &packet_size, sizeof(packet_size));
    if (result != ReadResult::Ok) {
        return result;
    }
    if (packet_size < 1 || packet_size > kMaximumPacketSize) {
        std::cerr << "Invalid video packet size: " << packet_size << '\n';
        errno = EPROTO;
        return ReadResult::Error;
    }

    result = read_full(fd, &packet.kind, sizeof(packet.kind));
    if (result != ReadResult::Ok) {
        return result;
    }
    packet_size -= sizeof(packet.kind);

    packet.timestamp = 0;
    if ((packet.kind == 'd' || packet.kind == 's') &&
        packet_size >= sizeof(packet.timestamp)) {
        result = read_full(fd, &packet.timestamp, sizeof(packet.timestamp));
        if (result != ReadResult::Ok) {
            return result;
        }
        packet_size -= sizeof(packet.timestamp);
    }

    packet.payload_size = packet_size;
    packet.payload.clear();

    if (packet.kind == 'd' && std::getenv("NO_PAYLOAD_WRITE") != nullptr) {
        packet.payload_size = 0;
        return ReadResult::Ok;
    }

    packet.payload.resize(packet_size);
    if (packet_size == 0) {
        return ReadResult::Ok;
    }
    return read_full(fd, packet.payload.data(), packet.payload.size());
}

struct FrameSample {
    double timestamp;
    double difference;
    double fps;
};

enum class DrainResult {
    Stopped,
    EndOfFile,
    BackendError,
    ReadError,
};

DrainResult drain_video_pipe(int fd) {
    std::uint64_t frame_count = 0;
    double previous_timestamp = 0.0;
    double difference = 0.0;
    int instant_fps = 0;
    std::deque<FrameSample> samples;

    while (!g_stop_requested) {
        VideoPacket packet;
        const ReadResult read_result = read_video_packet(fd, packet);
        if (read_result == ReadResult::Stopped) {
            return DrainResult::Stopped;
        }
        if (read_result == ReadResult::EndOfFile) {
            return DrainResult::EndOfFile;
        }
        if (read_result == ReadResult::Error) {
            std::perror("read video packet");
            return DrainResult::ReadError;
        }

        if (packet.kind == 'r') {
            std::cerr << "[RpiCamTs] mtxrpicam is ready\n";
            continue;
        }
        if (packet.kind == 'e') {
            const std::string message(packet.payload.begin(), packet.payload.end());
            std::cerr << "[RpiCamTs] mtxrpicam error";
            if (!message.empty()) {
                std::cerr << ": " << message;
            }
            std::cerr << '\n';
            return DrainResult::BackendError;
        }
        if (packet.kind != 'd') {
            continue;
        }

        ++frame_count;
        const double timestamp =
            static_cast<double>(packet.timestamp) / 1000000.0;
        if (previous_timestamp > 0.0) {
            difference = timestamp - previous_timestamp;
            if (difference > 0.0) {
                const double fps = 1.0 / difference;
                instant_fps = static_cast<int>(fps + 0.5);
                samples.push_back({timestamp, difference, fps});
            } else {
                samples.clear();
                instant_fps = 0;
            }
        }
        previous_timestamp = timestamp;

        while (!samples.empty() && timestamp - samples.front().timestamp > 2.0) {
            samples.pop_front();
        }

        double average_fps = 0.0;
        double minimum_fps = 0.0;
        double maximum_fps = 0.0;
        double average_difference = 0.0;
        double minimum_difference = 0.0;
        double maximum_difference = 0.0;

        for (const FrameSample &sample : samples) {
            average_fps += sample.fps;
            average_difference += sample.difference;
            if (minimum_fps == 0.0 || sample.fps < minimum_fps) {
                minimum_fps = sample.fps;
            }
            maximum_fps = std::max(maximum_fps, sample.fps);
            if (minimum_difference == 0.0 ||
                sample.difference < minimum_difference) {
                minimum_difference = sample.difference;
            }
            maximum_difference =
                std::max(maximum_difference, sample.difference);
        }
        if (!samples.empty()) {
            average_fps /= static_cast<double>(samples.size());
            average_difference /= static_cast<double>(samples.size());
        }

        const std::string nalu_information = collect_h264_nalu_info(
            packet.payload.data(), packet.payload.size());

        std::cout << std::setfill('0') << "Frame:" << std::setw(7)
                  << frame_count << " Fps:" << std::setw(2) << instant_fps
                  << std::setfill(' ') << std::fixed << std::setprecision(1)
                  << " AvgFps:" << average_fps << '/' << minimum_fps << '/'
                  << maximum_fps << std::setprecision(3)
                  << " AvgDiff:" << average_difference << '/'
                  << minimum_difference << '/' << maximum_difference
                  << " ts:" << timestamp << " Diff:" << difference
                  << " kind:'" << packet.kind << "' size:" << std::setfill('0')
                  << std::setw(8) << packet.payload_size << std::setfill(' ');
        if (!nalu_information.empty()) {
            std::cout << ' ' << nalu_information;
        }
        std::cout << '\n' << std::flush;
    }

    return DrainResult::Stopped;
}

void configure_child_environment(int config_fd, int video_fd) {
    const std::string config_fd_value = std::to_string(config_fd);
    const std::string video_fd_value = std::to_string(video_fd);
    setenv("PIPE_CONF_FD", config_fd_value.c_str(), 1);
    setenv("PIPE_VIDEO_FD", video_fd_value.c_str(), 1);

    const std::array<const char *, 7> library_paths = {
        "./third_party/mediamtx-rpicamera-fork/subprojects/libcamera/src/"
        "libcamera/base",
        "./third_party/mediamtx-rpicamera-fork/subprojects/libcamera/src/"
        "libcamera",
        "./third_party/mediamtx-rpicamera-fork/build/subprojects/libcamera/src/"
        "libcamera/base",
        "./third_party/mediamtx-rpicamera-fork/build/subprojects/libcamera/src/"
        "libcamera",
        "./build/mediamtx-rpicamera-fork/subprojects/libcamera/src/libcamera/"
        "base",
        "./build/mediamtx-rpicamera-fork/subprojects/libcamera/src/libcamera",
        "./dst",
    };
    for (const char *path : library_paths) {
        prepend_ld_library_path(path);
    }

    const std::array<const char *, 3> ipa_module_paths = {
        "./build/mediamtx-rpicamera-fork/install/lib/aarch64-linux-gnu/"
        "libcamera",
        "./build/mediamtx-rpicamera-fork/subprojects/libcamera/src/ipa/rpi/"
        "pisp",
        "./build/mediamtx-rpicamera-fork/subprojects/libcamera/src/ipa/rpi/"
        "vc4",
    };
    for (const char *path : ipa_module_paths) {
        if (access(path, R_OK) == 0) {
            prepend_environment_path("LIBCAMERA_IPA_MODULE_PATH", path);
        }
    }

    const char *ipa_config_path =
        "./build/mediamtx-rpicamera-fork/install/share/libcamera/ipa";
    if (access(ipa_config_path, R_OK) == 0) {
        prepend_environment_path("LIBCAMERA_IPA_CONFIG_PATH", ipa_config_path);
    }
    set_libpisp_config_file();
}

int wait_for_child(pid_t child_pid) {
    int status = 0;
    while (waitpid(child_pid, &status, 0) < 0) {
        if (errno == EINTR) {
            continue;
        }
        std::perror("waitpid");
        return 1;
    }

    if (WIFEXITED(status)) {
        const int exit_code = WEXITSTATUS(status);
        if (exit_code != 0) {
            std::cerr << "[RpiCamTs] mtxrpicam exited with status " << exit_code
                      << '\n';
        }
        return exit_code;
    }
    if (WIFSIGNALED(status)) {
        std::cerr << "[RpiCamTs] mtxrpicam terminated by signal "
                  << WTERMSIG(status) << '\n';
        return 1;
    }
    return 1;
}

} // namespace

int main(int argc, char **argv) {
    if (argc > 2) {
        std::cerr << "Usage: " << argv[0] << " [/path/to/mtxrpicam]\n";
        return 1;
    }

    std::string backend_path;
    if (argc == 2) {
        backend_path = real_path(argv[1]);
        if (backend_path.empty() || access(backend_path.c_str(), X_OK) != 0) {
            std::cerr << "mtxrpicam is not executable: " << argv[1] << '\n';
            return 1;
        }
    }

    chdir_to_project_root(argv[0]);
    if (backend_path.empty()) {
        backend_path = default_mtxrpicam_path();
    }
    if (backend_path.empty()) {
        std::cerr << "mtxrpicam was not found. Build the submodule with "
                     "./build.sh or pass its path as argv[1].\n";
        return 1;
    }

    if (!install_signal_handlers()) {
        std::perror("sigaction");
        return 1;
    }

    int config_pipe[2] = {-1, -1};
    int video_pipe[2] = {-1, -1};
    if (pipe(config_pipe) != 0) {
        std::perror("create config pipe");
        return 1;
    }
    if (pipe(video_pipe) != 0) {
        std::perror("create video pipe");
        close(config_pipe[0]);
        close(config_pipe[1]);
        return 1;
    }

    std::cerr << "[RpiCamTs] launching " << backend_path << '\n';
    const pid_t child_pid = fork();
    if (child_pid < 0) {
        std::perror("fork");
        close(config_pipe[0]);
        close(config_pipe[1]);
        close(video_pipe[0]);
        close(video_pipe[1]);
        return 1;
    }

    if (child_pid == 0) {
        close(config_pipe[1]);
        close(video_pipe[0]);
        signal(SIGINT, SIG_IGN);
        signal(SIGTERM, SIG_IGN);
        configure_child_environment(config_pipe[0], video_pipe[1]);
        execl(backend_path.c_str(), backend_path.c_str(),
              static_cast<char *>(nullptr));
        std::perror("exec mtxrpicam");
        _exit(127);
    }

    close(config_pipe[0]);
    close(video_pipe[1]);

    int result = 0;
    const std::string parameters = build_parameters();
    if (!write_packet(config_pipe[1], 'c', parameters)) {
        std::perror("write camera configuration");
        result = 1;
    } else {
        std::cerr << "[RpiCamTs] camera configuration sent\n";
        const DrainResult drain_result = drain_video_pipe(video_pipe[0]);
        if (drain_result == DrainResult::ReadError ||
            drain_result == DrainResult::BackendError) {
            result = 1;
        } else if (drain_result == DrainResult::EndOfFile &&
                   !g_stop_requested) {
            std::cerr << "[RpiCamTs] video pipe closed by mtxrpicam\n";
            result = 1;
        }
    }

    if (!write_packet(config_pipe[1], 'e', "") && errno != EPIPE) {
        std::perror("write stop packet");
        result = 1;
    }
    close(config_pipe[1]);
    close(video_pipe[0]);

    const int child_result = wait_for_child(child_pid);
    if (child_result != 0 && !g_stop_requested) {
        result = 1;
    }

    std::cerr << "[RpiCamTs] stopped\n";
    return result;
}
