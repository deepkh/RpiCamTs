#include "h264_file_writer.h"

#include <cerrno>
#include <cstring>

H264FileWriter::~H264FileWriter() { close(); }

bool H264FileWriter::open(const std::string &path,
                          std::string &error_message) {
    close();
    path_.clear();
    error_message.clear();

    errno = 0;
    file_ = std::fopen(path.c_str(), "wb");
    if (file_ == nullptr) {
        error_message = "'" + path + "': ";
        error_message += errno != 0 ? std::strerror(errno) : "open failed";
        return false;
    }

    path_ = path;
    return true;
}

bool H264FileWriter::write(const std::uint8_t *data, std::size_t size,
                           std::string &error_message) {
    error_message.clear();
    if (file_ == nullptr) {
        error_message = "output file is not open";
        return false;
    }
    if (size == 0) {
        return true;
    }

    errno = 0;
    if (std::fwrite(data, 1, size, file_) != size) {
        error_message = "'" + path_ + "': ";
        if (errno != 0) {
            error_message += std::strerror(errno);
        } else {
            error_message += "short write";
        }
        return false;
    }
    return true;
}

void H264FileWriter::close() {
    if (file_ != nullptr) {
        std::fclose(file_);
        file_ = nullptr;
    }
}

bool H264FileWriter::is_open() const { return file_ != nullptr; }

const std::string &H264FileWriter::path() const { return path_; }
