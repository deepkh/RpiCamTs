#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

struct TsMuxerConfig {
    std::string output_path;

    int width = 0;
    int height = 0;

    // Camera pipe timestamps are expressed in microseconds by default.
    int input_time_base_num = 1;
    int input_time_base_den = 1000000;
};

class TsMuxerFFmpeg {
  public:
    TsMuxerFFmpeg() = default;
    ~TsMuxerFFmpeg();

    TsMuxerFFmpeg(const TsMuxerFFmpeg &) = delete;
    TsMuxerFFmpeg &operator=(const TsMuxerFFmpeg &) = delete;

    bool open(const TsMuxerConfig &config, std::string &error_message);

    bool write_h264_packet(const std::uint8_t *data, std::size_t size,
                           std::uint64_t timestamp, bool is_keyframe,
                           std::string &error_message);

    void close();

    bool is_open() const;
    const std::string &path() const;

  private:
    struct Impl;
    Impl *impl_ = nullptr;
};
