#include "app.h"

#include "camera_params.h"
#include "h264_inspector.h"
#include "mtxrpicam_process.h"
#include "packet_io.h"
#include "path_utils.h"
#include "signal_handler.h"
#include "stats.h"

#include <cerrno>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <string>
#include <utility>

#include <unistd.h>

namespace {

enum class DrainResult {
    Stopped,
    EndOfFile,
    BackendError,
    ReadError,
};

DrainResult drain_video_pipe(int fd) {
    FrameStats stats;

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

        const FrameStatsSnapshot snapshot = stats.update(packet.timestamp);
        const std::string nalu_information = collect_h264_nalu_info(
            packet.payload.data(), packet.payload.size());

        std::cout << std::setfill('0') << "Frame:" << std::setw(7)
                  << snapshot.frame_count << " Fps:" << std::setw(2)
                  << static_cast<int>(snapshot.instant_fps)
                  << std::setfill(' ') << std::fixed << std::setprecision(1)
                  << " AvgFps:" << snapshot.avg_fps << '/' << snapshot.min_fps
                  << '/' << snapshot.max_fps << std::setprecision(3)
                  << " AvgDiff:" << snapshot.avg_diff << '/'
                  << snapshot.min_diff << '/' << snapshot.max_diff
                  << " ts:" << snapshot.timestamp_sec
                  << " Diff:" << snapshot.diff_sec << " kind:'" << packet.kind
                  << "' size:" << std::setfill('0') << std::setw(8)
                  << packet.payload_size << std::setfill(' ');
        if (!nalu_information.empty()) {
            std::cout << ' ' << nalu_information;
        }
        std::cout << '\n' << std::flush;
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
    std::string backend_path;
    if (!options_.mtxrpicam_path.empty()) {
        backend_path = absolute_path(options_.mtxrpicam_path);
        if (backend_path.empty() || !is_executable(backend_path)) {
            std::cerr << "mtxrpicam is not executable: "
                      << options_.mtxrpicam_path << '\n';
            return 1;
        }
    }

    chdir_to_project_root();
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
    const std::string parameters = build_camera_parameters();
    if (!write_config_packet(config_pipe[1], parameters)) {
        std::perror("write camera configuration");
        result = 1;
    } else {
        std::cerr << "[RpiCamTs] camera configuration sent\n";
        const DrainResult drain_result = drain_video_pipe(video_pipe[0]);
        if (drain_result == DrainResult::ReadError ||
            drain_result == DrainResult::BackendError) {
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

    std::cerr << "[RpiCamTs] stopped\n";
    return result;
}
