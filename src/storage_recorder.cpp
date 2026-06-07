#include "storage_recorder.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <system_error>

#include <sys/file.h>
#include <unistd.h>

class StorageRecorder::StorageLock {
  public:
    bool lock(const std::filesystem::path &lock_path,
              std::string &error_message) {
        unlock();
        fd_ = ::open(lock_path.c_str(), O_CREAT | O_RDWR, 0644);
        if (fd_ < 0) {
            error_message = "failed to open storage lock '" +
                            lock_path.string() + "': " + std::strerror(errno);
            return false;
        }

        if (flock(fd_, LOCK_EX | LOCK_NB) != 0) {
            const int lock_error = errno;
            unlock();
            if (lock_error == EWOULDBLOCK || lock_error == EAGAIN) {
                error_message = "storage directory is already in use: " +
                                lock_path.parent_path().string();
            } else {
                error_message = "failed to lock storage directory '" +
                                lock_path.parent_path().string() + "': " +
                                std::strerror(lock_error);
            }
            return false;
        }

        return true;
    }

    void unlock() {
        if (fd_ < 0) {
            return;
        }
        flock(fd_, LOCK_UN);
        ::close(fd_);
        fd_ = -1;
    }

    ~StorageLock() { unlock(); }

  private:
    int fd_ = -1;
};

StorageRecorder::StorageRecorder() = default;

StorageRecorder::~StorageRecorder() { close(); }

bool StorageRecorder::open(const std::filesystem::path &storage_root,
                           const TsMuxerConfig &base_ts_config,
                           std::string &error_message) {
    close();
    error_message.clear();
    storage_root_ = storage_root;
    base_ts_config_ = base_ts_config;

    std::error_code error;
    if (std::filesystem::exists(storage_root_, error)) {
        if (error) {
            error_message = "failed to inspect storage directory '" +
                            storage_root_.string() + "': " + error.message();
            return false;
        }
        if (!std::filesystem::is_directory(storage_root_, error) || error) {
            error_message = "storage path is not a directory: " +
                            storage_root_.string();
            return false;
        }
    } else {
        std::filesystem::create_directories(storage_root_, error);
        if (error) {
            error_message = "failed to create storage directory '" +
                            storage_root_.string() + "': " + error.message();
            return false;
        }
    }

    lock_ = std::make_unique<StorageLock>();
    if (!lock_->lock(storage_root_ / ".lock", error_message)) {
        close();
        return false;
    }
    if (!index_.load_or_create(storage_root_, error_message) ||
        !open_next_segment(error_message)) {
        close();
        return false;
    }

    return true;
}

bool StorageRecorder::write_h264_packet(const std::uint8_t *data,
                                        std::size_t size,
                                        std::uint64_t timestamp,
                                        bool is_keyframe,
                                        std::string &error_message) {
    if (!open_) {
        error_message = "managed storage recorder is not open";
        return false;
    }
    if (!muxer_.write_h264_packet(data, size, timestamp, is_keyframe,
                                  error_message)) {
        return false;
    }
    return rotate_if_needed(error_message);
}

void StorageRecorder::close() {
    muxer_.close();
    open_ = false;
    current_output_path_.clear();
    if (lock_ != nullptr) {
        lock_->unlock();
        lock_.reset();
    }
}

bool StorageRecorder::open_next_segment(std::string &error_message) {
    const StorageRecordPath next = index_.next_record_path(error_message);
    if (!error_message.empty()) {
        return false;
    }

    std::error_code directory_error;
    std::filesystem::create_directories(next.absolute_path.parent_path(),
                                        directory_error);
    if (directory_error) {
        error_message = "failed to create managed storage folder '" +
                        next.absolute_path.parent_path().string() + "': " +
                        directory_error.message();
        return false;
    }

    const int reserved_fd = ::open(next.absolute_path.c_str(),
                                   O_CREAT | O_EXCL | O_WRONLY, 0644);
    if (reserved_fd < 0) {
        error_message = "failed to reserve managed record path '" +
                        next.absolute_path.string() + "': " +
                        std::strerror(errno);
        return false;
    }
    if (::close(reserved_fd) != 0) {
        error_message = "failed to close managed record placeholder '" +
                        next.absolute_path.string() + "': " +
                        std::strerror(errno);
        std::error_code ignored;
        std::filesystem::remove(next.absolute_path, ignored);
        return false;
    }

    if (!index_.append_record(next.folder_index, next.filename,
                              error_message) ||
        !index_.save_atomic(error_message)) {
        std::error_code ignored;
        std::filesystem::remove(next.absolute_path, ignored);
        return false;
    }

    TsMuxerConfig segment_config = base_ts_config_;
    segment_config.output_path = next.absolute_path.string();
    current_output_path_ = next.absolute_path;
    if (!muxer_.open(segment_config, error_message)) {
        return false;
    }
    open_ = true;
    return true;
}

bool StorageRecorder::rotate_if_needed(std::string &error_message) {
    std::error_code error;
    const std::uintmax_t size =
        std::filesystem::file_size(current_output_path_, error);
    if (error) {
        error_message = "failed to read managed record size '" +
                        current_output_path_.string() + "': " + error.message();
        return false;
    }
    if (size < index_.file_segmentation_size_bytes()) {
        return true;
    }

    muxer_.close();
    open_ = false;
    return open_next_segment(error_message);
}
