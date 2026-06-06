#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

class H264FileWriter {
  public:
    H264FileWriter() = default;
    ~H264FileWriter();

    H264FileWriter(const H264FileWriter &) = delete;
    H264FileWriter &operator=(const H264FileWriter &) = delete;

    bool open(const std::string &path, std::string &error_message);
    bool write(const std::uint8_t *data, std::size_t size,
               std::string &error_message);
    void close();

    bool is_open() const;
    const std::string &path() const;

  private:
    std::FILE *file_ = nullptr;
    std::string path_;
};
