#include "app.h"

#include "camera_params.h"
#include "config_loader.h"
#include "default_config.h"
#include "h264_file_writer.h"
#include "h264_inspector.h"
#include "mtxrpicam_process.h"
#include "packet_io.h"
#include "path_utils.h"
#include "signal_handler.h"
#include "stats.h"
#include "storage_recorder.h"
#include "ts_muxer_ffmpeg.h"

#include <cerrno>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <string>
#include <stdexcept>
#include <utility>
#include <vector>

#include <unistd.h>

namespace {

enum class DrainResult {
    Stopped,
    EndOfFile,
    BackendError,
    ReadError,
    WriteError,
};

DrainResult drain_video_pipe(int fd, OutputMode output_mode,
                             H264FileWriter &h264_writer,
                             TsMuxerFFmpeg &ts_muxer,
                             StorageRecorder *storage_recorder, bool verbose) {
    FrameStats stats;
    std::vector<std::uint8_t> pending_h264_prefix;

    while (!stop_requested()) {
        VideoPacket packet;
        const PacketReadResult read_result = read_video_packet(fd, packet);
        if (read_result == PacketReadResult::Stopped) {
            return DrainResult::Stopped;
        }
        if (read_result == PacketReadResult::EndOfFile) {
            return DrainResult::EndOfFile;
        }
        if (read_result == PacketReadResult::Error) {
            std::perror("read video packet");
            return DrainResult::ReadError;
        }

        if (packet.kind == 'r') {
            std::cerr << "[RpiCamTs] mtxrpicam is ready\n";
            continue;
        }
        if (packet.kind == 'e') {
            const std::string message(packet.payload.begin(),
                                      packet.payload.end());
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

        if (!packet.payload.empty() && output_mode == OutputMode::RawH264) {
            std::string error_message;
            if (!h264_writer.write(packet.payload.data(), packet.payload.size(),
                                   error_message)) {
                std::cerr << "[RpiCamTs] Failed to write H264 output: "
                          << error_message << '\n';
                return DrainResult::WriteError;
            }
        } else if (!packet.payload.empty()) {
            if (!packet.has_timestamp) {
                std::cerr << "[RpiCamTs] Warning: skipping H264 packet without "
                             "a camera timestamp for MPEG-TS output\n";
            } else if (!h264_looks_like_annex_b(packet.payload.data(),
                                                packet.payload.size())) {
                std::cerr << "[RpiCamTs] Warning: skipping H264 packet that "
                             "does not contain an Annex B start code\n";
            } else if (!h264_contains_video_frame(packet.payload.data(),
                                                  packet.payload.size())) {
                pending_h264_prefix.insert(pending_h264_prefix.end(),
                                           packet.payload.begin(),
                                           packet.payload.end());
            } else {
                std::string error_message;
                const bool is_keyframe = h264_contains_idr_frame(
                    packet.payload.data(), packet.payload.size());

                const std::uint8_t *data = packet.payload.data();
                std::size_t size = packet.payload.size();
                std::vector<std::uint8_t> access_unit;
                if (!pending_h264_prefix.empty()) {
                    access_unit.reserve(pending_h264_prefix.size() + size);
                    access_unit.insert(access_unit.end(),
                                       pending_h264_prefix.begin(),
                                       pending_h264_prefix.end());
                    access_unit.insert(access_unit.end(), packet.payload.begin(),
                                       packet.payload.end());
                    pending_h264_prefix.clear();
                    data = access_unit.data();
                    size = access_unit.size();
                }

                const bool write_ok = storage_recorder != nullptr
                                          ? storage_recorder->write_h264_packet(
                                                data, size, packet.timestamp,
                                                is_keyframe, error_message)
                                          : ts_muxer.write_h264_packet(
                                                data, size, packet.timestamp,
                                                is_keyframe, error_message);
                if (!write_ok) {
                    std::cerr
                        << (storage_recorder != nullptr
                                ? "[RpiCamTs] Failed to write managed MPEG-TS "
                                  "storage output: "
                                : "[RpiCamTs] Failed to write MPEG-TS output: ")
                        << error_message << '\n';
                    return DrainResult::WriteError;
                }
            }
        }

        if (verbose) {
            const FrameStatsSnapshot snapshot = stats.update(packet.timestamp);
            const std::string nalu_information = collect_h264_nalu_info(
                packet.payload.data(), packet.payload.size());

            std::cout << std::setfill('0') << "Frame:" << std::setw(7)
                      << snapshot.frame_count << " Fps:" << std::setw(2)
                      << static_cast<int>(snapshot.instant_fps)
                      << std::setfill(' ') << std::fixed
                      << std::setprecision(1) << " AvgFps:" << snapshot.avg_fps
                      << '/' << snapshot.min_fps << '/' << snapshot.max_fps
                      << std::setprecision(3) << " AvgDiff:"
                      << snapshot.avg_diff << '/' << snapshot.min_diff << '/'
                      << snapshot.max_diff << " ts:" << snapshot.timestamp_sec
                      << " Diff:" << snapshot.diff_sec << " kind:'"
                      << packet.kind << "' size:" << std::setfill('0')
                      << std::setw(8) << packet.payload_size
                      << std::setfill(' ');
            if (!nalu_information.empty()) {
                std::cout << ' ' << nalu_information;
            }
            std::cout << '\n' << std::flush;
        }
    }

    return DrainResult::Stopped;
}

void close_fd(int &fd) {
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
}

} // namespace

RpiCamTsApp::RpiCamTsApp(RpiCamTsOptions options)
    : options_(std::move(options)) {}

int RpiCamTsApp::run() {
    if (options_.generate_default_config) {
        std::string error_message;
        if (!write_default_config_file(options_.generate_default_config_path,
                                       error_message)) {
            std::cerr << "[RpiCamTs] " << error_message << '\n';
            return 1;
        }

        std::cout << "[RpiCamTs] Generated default config: "
                  << options_.generate_default_config_path << '\n';
        return 0;
    }

    std::string config_path = options_.config_path;
    if (options_.config_path_explicit) {
        config_path = absolute_path(config_path);
        if (config_path.empty()) {
            std::cerr << "[RpiCamTs] Config file not found: "
                      << options_.config_path << '\n';
            return 1;
        }
    }

    if (options_.storage_mode) {
        const std::string storage_path =
            absolute_path_allow_missing(options_.storage_path);
        if (storage_path.empty()) {
            std::cerr << "[RpiCamTs] Invalid storage path: "
                      << options_.storage_path << '\n';
            return 1;
        }
        options_.storage_path = storage_path;
    } else if (options_.output_path_explicit) {
        const std::string output_path =
            absolute_path_allow_missing(options_.output_path);
        if (output_path.empty()) {
            std::cerr << "[RpiCamTs] Invalid output path: "
                      << options_.output_path << '\n';
            return 1;
        }
        options_.output_path = output_path;
    }

    chdir_to_project_root();

    ConfigLoadResult config;
    if (!config_path.empty() && config_file_exists(config_path)) {
        try {
            config = load_config_file(config_path);
        } catch (const std::runtime_error &error) {
            std::cerr << "[RpiCamTs] Failed to load config: " << error.what()
                      << '\n';
            return 1;
        }
        std::cerr << "[RpiCamTs] Loaded config: " << config.path << '\n';
    } else if (options_.config_path_explicit) {
        std::cerr << "[RpiCamTs] Config file not found: "
                  << options_.config_path << '\n';
        return 1;
    } else if (!config_path.empty()) {
        std::cerr << "[RpiCamTs] Default config not found: " << config_path
                  << ", using built-in defaults\n";
    }

    std::string backend_path;
    const auto configured_backend = config.values.find("MtxRpiCamPath");
    if (configured_backend != config.values.end() &&
        !configured_backend->second.empty()) {
        backend_path = absolute_path(configured_backend->second);
        if (backend_path.empty() || !is_executable(backend_path)) {
            std::cerr << "[RpiCamTs] mtxrpicam is not executable: "
                      << configured_backend->second << '\n';
            return 1;
        }
    }

    if (backend_path.empty()) {
        backend_path = default_mtxrpicam_path();
    }
    if (backend_path.empty()) {
        std::cerr << "mtxrpicam was not found. Build the submodule with "
                     "./build.sh or set MtxRpiCamPath in the config file.\n";
        return 1;
    }

    H264FileWriter h264_writer;
    TsMuxerFFmpeg ts_muxer;
    StorageRecorder storage_recorder;
    std::string output_error;
    if (options_.output_mode == OutputMode::RawH264) {
        if (!h264_writer.open(options_.output_path, output_error)) {
            std::cerr << "[RpiCamTs] Failed to open H264 output file: "
                      << output_error << '\n';
            return 1;
        }
        std::cout << "[RpiCamTs] Writing raw H264 stream to: "
                  << h264_writer.path() << '\n' << std::flush;
    } else {
        TsMuxerConfig ts_config;
        ts_config.width = get_camera_param_int(config.values, "Width", 2304);
        ts_config.height = get_camera_param_int(config.values, "Height", 1296);
        if (options_.storage_mode) {
            if (!storage_recorder.open(options_.storage_path, ts_config,
                                       output_error)) {
                std::cerr << "[RpiCamTs] Failed to open managed MPEG-TS "
                             "storage: "
                          << output_error << '\n';
                return 1;
            }
            std::cout << "[RpiCamTs] Writing managed MPEG-TS storage to: "
                      << options_.storage_path << '\n'
                      << std::flush;
        } else {
            ts_config.output_path = options_.output_path;
            if (!ts_muxer.open(ts_config, output_error)) {
                std::cerr << "[RpiCamTs] Failed to open MPEG-TS output file: "
                          << output_error << '\n';
                return 1;
            }
            std::cout << "[RpiCamTs] Writing MPEG-TS stream to: "
                      << ts_muxer.path() << '\n'
                      << std::flush;
        }
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
        close_fd(config_pipe[0]);
        close_fd(config_pipe[1]);
        return 1;
    }

    std::cerr << "[RpiCamTs] launching " << backend_path << '\n';
    MtxRpiCamProcess process({backend_path, config_pipe[0], video_pipe[1],
                              config_pipe[1], video_pipe[0]});
    if (!process.start()) {
        std::perror("fork");
        close_fd(config_pipe[0]);
        close_fd(config_pipe[1]);
        close_fd(video_pipe[0]);
        close_fd(video_pipe[1]);
        return 1;
    }

    close_fd(config_pipe[0]);
    close_fd(video_pipe[1]);

    int result = 0;
    const std::string parameters = build_camera_parameters(config.values);
    if (!write_config_packet(config_pipe[1], parameters)) {
        std::perror("write camera configuration");
        result = 1;
    } else {
        std::cerr << "[RpiCamTs] camera configuration sent\n";
        const DrainResult drain_result = drain_video_pipe(
            video_pipe[0], options_.output_mode, h264_writer, ts_muxer,
            options_.storage_mode ? &storage_recorder : nullptr,
            options_.verbose);
        if (drain_result == DrainResult::ReadError ||
            drain_result == DrainResult::BackendError ||
            drain_result == DrainResult::WriteError) {
            result = 1;
        } else if (drain_result == DrainResult::EndOfFile &&
                   !stop_requested()) {
            std::cerr << "[RpiCamTs] video pipe closed by mtxrpicam\n";
            result = 1;
        }
    }

    if (!write_end_packet(config_pipe[1]) && errno != EPIPE) {
        std::perror("write stop packet");
        result = 1;
    }
    close_fd(config_pipe[1]);
    close_fd(video_pipe[0]);

    const int child_result = process.wait();
    if (child_result != 0 && !stop_requested()) {
        result = 1;
    }

    h264_writer.close();
    ts_muxer.close();
    storage_recorder.close();
    std::cerr << "[RpiCamTs] stopped\n";
    return result;
}
