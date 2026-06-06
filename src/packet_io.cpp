#include "packet_io.h"

#include "signal_handler.h"

#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <limits>

#include <unistd.h>

namespace {

constexpr std::size_t kMaximumPacketSize = 256U * 1024U * 1024U;

bool write_full(int fd, const void *buffer, std::size_t size) {
    const auto *data = static_cast<const std::uint8_t *>(buffer);
    while (size > 0) {
        const ssize_t written = write(fd, data, size);
        if (written < 0) {
            if (errno == EINTR && !stop_requested()) {
                continue;
            }
            return false;
        }
        if (written == 0) {
            errno = EIO;
            return false;
        }
        data += written;
        size -= static_cast<std::size_t>(written);
    }
    return true;
}

bool write_packet(int fd, char kind, const std::string &payload) {
    if (payload.size() >
        std::numeric_limits<std::uint32_t>::max() - sizeof(kind)) {
        errno = EOVERFLOW;
        return false;
    }

    const std::uint32_t size =
        static_cast<std::uint32_t>(sizeof(kind) + payload.size());
    return write_full(fd, &size, sizeof(size)) &&
           write_full(fd, &kind, sizeof(kind)) &&
           write_full(fd, payload.data(), payload.size());
}

PacketReadResult read_full(int fd, void *buffer, std::size_t size) {
    auto *data = static_cast<std::uint8_t *>(buffer);
    while (size > 0) {
        const ssize_t count = read(fd, data, size);
        if (count < 0) {
            if (errno == EINTR) {
                if (stop_requested()) {
                    return PacketReadResult::Stopped;
                }
                continue;
            }
            return PacketReadResult::Error;
        }
        if (count == 0) {
            return PacketReadResult::EndOfFile;
        }
        data += count;
        size -= static_cast<std::size_t>(count);
    }
    return PacketReadResult::Ok;
}

} // namespace

bool write_config_packet(int fd, const std::string &parameters) {
    return write_packet(fd, 'c', parameters);
}

bool write_end_packet(int fd) { return write_packet(fd, 'e', ""); }

PacketReadResult read_video_packet(int fd, VideoPacket &packet) {
    std::uint32_t packet_size = 0;
    PacketReadResult result = read_full(fd, &packet_size, sizeof(packet_size));
    if (result != PacketReadResult::Ok) {
        return result;
    }
    if (packet_size < 1 || packet_size > kMaximumPacketSize) {
        std::cerr << "Invalid video packet size: " << packet_size << '\n';
        errno = EPROTO;
        return PacketReadResult::Error;
    }

    result = read_full(fd, &packet.kind, sizeof(packet.kind));
    if (result != PacketReadResult::Ok) {
        return result;
    }
    packet_size -= sizeof(packet.kind);

    packet.timestamp = 0;
    packet.has_timestamp = false;
    if ((packet.kind == 'd' || packet.kind == 's') &&
        packet_size >= sizeof(packet.timestamp)) {
        result = read_full(fd, &packet.timestamp, sizeof(packet.timestamp));
        if (result != PacketReadResult::Ok) {
            return result;
        }
        packet_size -= sizeof(packet.timestamp);
        packet.has_timestamp = true;
    }

    packet.payload_size = packet_size;
    packet.payload.clear();

    if (packet.kind == 'd' && std::getenv("NO_PAYLOAD_WRITE") != nullptr) {
        packet.payload_size = 0;
        return PacketReadResult::Ok;
    }

    packet.payload.resize(packet_size);
    if (packet_size == 0) {
        return PacketReadResult::Ok;
    }
    return read_full(fd, packet.payload.data(), packet.payload.size());
}
