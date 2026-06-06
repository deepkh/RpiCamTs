#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct VideoPacket {
    char kind = '\0';
    std::uint64_t timestamp = 0;
    std::vector<std::uint8_t> payload;
    bool has_timestamp = false;
    std::size_t payload_size = 0;
};

enum class PacketReadResult {
    Ok,
    EndOfFile,
    Stopped,
    Error,
};

bool write_config_packet(int fd, const std::string &parameters);
bool write_end_packet(int fd);
PacketReadResult read_video_packet(int fd, VideoPacket &packet);
