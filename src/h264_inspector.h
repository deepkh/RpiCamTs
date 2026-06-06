#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

std::string collect_h264_nalu_info(const std::uint8_t *data,
                                   std::size_t size);
