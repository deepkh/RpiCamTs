#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

std::string collect_h264_nalu_info(const std::uint8_t *data,
                                   std::size_t size);

bool h264_contains_idr_frame(const std::uint8_t *data, std::size_t size);
bool h264_contains_video_frame(const std::uint8_t *data, std::size_t size);
bool h264_looks_like_annex_b(const std::uint8_t *data, std::size_t size);
