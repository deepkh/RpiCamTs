#pragma once

#include "storage_index.h"
#include "ts_muxer_ffmpeg.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

class StorageRecorder {
  public:
    StorageRecorder();
    ~StorageRecorder();

    StorageRecorder(const StorageRecorder &) = delete;
    StorageRecorder &operator=(const StorageRecorder &) = delete;

    bool open(const std::filesystem::path &storage_root,
              const TsMuxerConfig &base_ts_config,
              std::string &error_message);

    bool write_h264_packet(const std::uint8_t *data, std::size_t size,
                           std::uint64_t timestamp, bool is_keyframe,
                           std::string &error_message);

    void close();

  private:
    class StorageLock;

    bool open_next_segment(std::string &error_message);
    bool rotate_if_needed(std::string &error_message);

    std::filesystem::path storage_root_;
    std::filesystem::path current_output_path_;
    TsMuxerConfig base_ts_config_;
    TsMuxerFFmpeg muxer_;
    StorageIndex index_;
    std::unique_ptr<StorageLock> lock_;
    bool open_ = false;
};
