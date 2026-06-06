#include "h264_inspector.h"

#include <algorithm>
#include <array>
#include <vector>

namespace {

class BitReader {
  public:
    BitReader(const std::uint8_t *data, std::size_t size)
        : data_(data), size_(size) {}

    bool read_unsigned_exp_golomb(std::uint32_t &value) {
        std::uint32_t leading_zero_bits = 0;
        bool bit = false;
        while (true) {
            if (!read_bit(bit)) {
                return false;
            }
            if (bit) {
                break;
            }
            if (++leading_zero_bits >= 32) {
                return false;
            }
        }

        value = (1U << leading_zero_bits) - 1U;
        for (std::uint32_t index = 0; index < leading_zero_bits; ++index) {
            if (!read_bit(bit)) {
                return false;
            }
            if (bit) {
                value += 1U << (leading_zero_bits - index - 1U);
            }
        }
        return true;
    }

  private:
    bool read_bit(bool &bit) {
        if (bit_offset_ >= size_ * 8U) {
            return false;
        }
        const std::size_t byte_offset = bit_offset_ / 8U;
        const std::size_t bit_in_byte = 7U - (bit_offset_ % 8U);
        bit = ((data_[byte_offset] >> bit_in_byte) & 0x01U) != 0;
        ++bit_offset_;
        return true;
    }

    const std::uint8_t *data_;
    std::size_t size_;
    std::size_t bit_offset_ = 0;
};

std::size_t h264_start_code_size(const std::uint8_t *buffer,
                                 std::size_t size, std::size_t offset) {
    if (offset + 3 <= size && buffer[offset] == 0x00 &&
        buffer[offset + 1] == 0x00 && buffer[offset + 2] == 0x01) {
        return 3;
    }
    if (offset + 4 <= size && buffer[offset] == 0x00 &&
        buffer[offset + 1] == 0x00 && buffer[offset + 2] == 0x00 &&
        buffer[offset + 3] == 0x01) {
        return 4;
    }
    return 0;
}

std::vector<std::uint8_t> h264_copy_rbsp(const std::uint8_t *payload,
                                         std::size_t size) {
    std::vector<std::uint8_t> rbsp;
    rbsp.reserve(std::min<std::size_t>(size, 64));
    unsigned int zero_count = 0;

    for (std::size_t index = 0; index < size && rbsp.size() < 64; ++index) {
        if (zero_count >= 2 && payload[index] == 0x03) {
            zero_count = 0;
            continue;
        }

        rbsp.push_back(payload[index]);
        if (payload[index] == 0x00) {
            ++zero_count;
        } else {
            zero_count = 0;
        }
    }
    return rbsp;
}

std::string h264_slice_type_name(const std::uint8_t *payload,
                                 std::size_t size) {
    const std::vector<std::uint8_t> rbsp = h264_copy_rbsp(payload, size);
    BitReader reader(rbsp.data(), rbsp.size());
    std::uint32_t first_macroblock = 0;
    std::uint32_t slice_type = 0;
    if (!reader.read_unsigned_exp_golomb(first_macroblock) ||
        !reader.read_unsigned_exp_golomb(slice_type)) {
        return {};
    }

    static const std::array<const char *, 5> names = {"P", "B", "I", "SP",
                                                       "SI"};
    return names[slice_type % names.size()];
}

} // namespace

std::string collect_h264_nalu_info(const std::uint8_t *buffer,
                                   std::size_t size) {
    std::string information;
    const auto append = [&information](const std::string &value) {
        if (value.empty()) {
            return;
        }
        if (!information.empty()) {
            information.push_back(' ');
        }
        information += value;
    };

    std::size_t offset = 0;
    while (offset < size) {
        const std::size_t start_code_size =
            h264_start_code_size(buffer, size, offset);
        if (start_code_size == 0) {
            ++offset;
            continue;
        }

        const std::size_t header_offset = offset + start_code_size;
        if (header_offset >= size) {
            break;
        }

        std::size_t next_start_code = header_offset + 1;
        while (next_start_code < size &&
               h264_start_code_size(buffer, size, next_start_code) == 0) {
            ++next_start_code;
        }

        const std::uint8_t type = buffer[header_offset] & 0x1fU;
        switch (type) {
        case 6:
            append("SEI");
            break;
        case 7:
            append("SPS");
            break;
        case 8:
            append("PPS");
            break;
        case 9:
            append("AUD");
            break;
        default:
            break;
        }

        if ((type == 1 || type == 5) && header_offset + 1 <= next_start_code) {
            append(h264_slice_type_name(
                buffer + header_offset + 1,
                next_start_code - (header_offset + 1)));
        }

        offset = next_start_code;
    }
    return information;
}
