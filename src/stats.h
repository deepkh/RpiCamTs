#pragma once

#include <cstdint>
#include <deque>

struct FrameStatsSnapshot {
    std::uint64_t frame_count = 0;

    double timestamp_sec = 0.0;
    double diff_sec = 0.0;

    double instant_fps = 0.0;

    double avg_fps = 0.0;
    double min_fps = 0.0;
    double max_fps = 0.0;

    double avg_diff = 0.0;
    double min_diff = 0.0;
    double max_diff = 0.0;
};

class FrameStats {
  public:
    FrameStatsSnapshot update(std::uint64_t timestamp_us);

  private:
    struct Sample {
        double timestamp;
        double difference;
        double fps;
    };

    std::uint64_t frame_count_ = 0;
    double previous_timestamp_ = 0.0;
    double difference_ = 0.0;
    double instant_fps_ = 0.0;
    std::deque<Sample> samples_;
};
