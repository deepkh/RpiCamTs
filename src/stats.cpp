#include "stats.h"

#include <algorithm>

FrameStatsSnapshot FrameStats::update(std::uint64_t timestamp_us) {
    ++frame_count_;
    const double timestamp = static_cast<double>(timestamp_us) / 1000000.0;
    if (previous_timestamp_ > 0.0) {
        difference_ = timestamp - previous_timestamp_;
        if (difference_ > 0.0) {
            const double fps = 1.0 / difference_;
            instant_fps_ = static_cast<int>(fps + 0.5);
            samples_.push_back({timestamp, difference_, fps});
        } else {
            samples_.clear();
            instant_fps_ = 0.0;
        }
    }
    previous_timestamp_ = timestamp;

    while (!samples_.empty() &&
           timestamp - samples_.front().timestamp > 2.0) {
        samples_.pop_front();
    }

    FrameStatsSnapshot snapshot;
    snapshot.frame_count = frame_count_;
    snapshot.timestamp_sec = timestamp;
    snapshot.diff_sec = difference_;
    snapshot.instant_fps = instant_fps_;

    for (const Sample &sample : samples_) {
        snapshot.avg_fps += sample.fps;
        snapshot.avg_diff += sample.difference;
        if (snapshot.min_fps == 0.0 || sample.fps < snapshot.min_fps) {
            snapshot.min_fps = sample.fps;
        }
        snapshot.max_fps = std::max(snapshot.max_fps, sample.fps);
        if (snapshot.min_diff == 0.0 ||
            sample.difference < snapshot.min_diff) {
            snapshot.min_diff = sample.difference;
        }
        snapshot.max_diff = std::max(snapshot.max_diff, sample.difference);
    }
    if (!samples_.empty()) {
        snapshot.avg_fps /= static_cast<double>(samples_.size());
        snapshot.avg_diff /= static_cast<double>(samples_.size());
    }

    return snapshot;
}
