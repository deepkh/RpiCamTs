#include "ts_muxer_ffmpeg.h"

#include <cstring>
#include <iostream>
#include <limits>
#include <new>

extern "C" {
#include <libavcodec/codec_id.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
}

namespace {

std::string ffmpeg_error_string(int error_code) {
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    if (av_strerror(error_code, buffer, sizeof(buffer)) < 0) {
        return "FFmpeg error " + std::to_string(error_code);
    }
    return buffer;
}

} // namespace

struct TsMuxerFFmpeg::Impl {
    AVFormatContext *format_context = nullptr;
    AVStream *stream = nullptr;
    std::string output_path;
    AVRational input_time_base{1, 1000000};
    bool header_written = false;
    bool have_first_timestamp = false;
    std::uint64_t first_timestamp = 0;
    std::int64_t last_dts = AV_NOPTS_VALUE;
};

TsMuxerFFmpeg::~TsMuxerFFmpeg() { close(); }

bool TsMuxerFFmpeg::open(const TsMuxerConfig &config,
                         std::string &error_message) {
    close();
    error_message.clear();

    if (config.output_path.empty()) {
        error_message = "output path is empty";
        return false;
    }
    if (config.width <= 0 || config.height <= 0) {
        error_message = "video width and height must be positive";
        return false;
    }
    if (config.input_time_base_num <= 0 || config.input_time_base_den <= 0) {
        error_message = "input time base must be positive";
        return false;
    }

    impl_ = new (std::nothrow) Impl;
    if (impl_ == nullptr) {
        error_message = "failed to allocate TS muxer state";
        return false;
    }
    impl_->output_path = config.output_path;
    impl_->input_time_base =
        AVRational{config.input_time_base_num, config.input_time_base_den};

    int result = avformat_alloc_output_context2(
        &impl_->format_context, nullptr, "mpegts", config.output_path.c_str());
    if (result < 0 || impl_->format_context == nullptr) {
        error_message = "failed to allocate MPEG-TS output context: " +
                        ffmpeg_error_string(result < 0 ? result : AVERROR_UNKNOWN);
        close();
        return false;
    }

    impl_->stream = avformat_new_stream(impl_->format_context, nullptr);
    if (impl_->stream == nullptr) {
        error_message = "failed to create H264 output stream";
        close();
        return false;
    }

    impl_->stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    impl_->stream->codecpar->codec_id = AV_CODEC_ID_H264;
    impl_->stream->codecpar->codec_tag = 0;
    impl_->stream->codecpar->width = config.width;
    impl_->stream->codecpar->height = config.height;
    impl_->stream->time_base = AVRational{1, 90000};

    if ((impl_->format_context->oformat->flags & AVFMT_NOFILE) == 0) {
        result = avio_open(&impl_->format_context->pb, config.output_path.c_str(),
                           AVIO_FLAG_WRITE);
        if (result < 0) {
            error_message = "failed to open MPEG-TS output '" +
                            config.output_path + "': " +
                            ffmpeg_error_string(result);
            close();
            return false;
        }
    }

    result = avformat_write_header(impl_->format_context, nullptr);
    if (result < 0) {
        error_message = "failed to write MPEG-TS header: " +
                        ffmpeg_error_string(result);
        close();
        return false;
    }
    impl_->header_written = true;
    return true;
}

bool TsMuxerFFmpeg::write_h264_packet(const std::uint8_t *data,
                                      std::size_t size,
                                      std::uint64_t timestamp,
                                      bool is_keyframe,
                                      std::string &error_message) {
    error_message.clear();
    if (!is_open()) {
        error_message = "MPEG-TS muxer is not open";
        return false;
    }
    if (data == nullptr || size == 0) {
        error_message = "H264 packet is empty";
        return false;
    }
    if (size > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        error_message = "H264 packet is too large for FFmpeg";
        return false;
    }

    if (!impl_->have_first_timestamp) {
        impl_->first_timestamp = timestamp;
        impl_->have_first_timestamp = true;
    }

    std::uint64_t normalized_timestamp = 0;
    if (timestamp < impl_->first_timestamp) {
        std::cerr << "[RpiCamTs] Warning: camera timestamp moved before the "
                     "first timestamp; clamping TS timestamp to zero\n";
    } else {
        normalized_timestamp = timestamp - impl_->first_timestamp;
    }

    if (normalized_timestamp >
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        error_message = "normalized camera timestamp exceeds FFmpeg range";
        return false;
    }

    std::int64_t pts = av_rescale_q(
        static_cast<std::int64_t>(normalized_timestamp), impl_->input_time_base,
        impl_->stream->time_base);
    if (impl_->last_dts != AV_NOPTS_VALUE && pts <= impl_->last_dts) {
        if (impl_->last_dts == std::numeric_limits<std::int64_t>::max()) {
            error_message = "MPEG-TS timestamp exceeds FFmpeg range";
            return false;
        }
        std::cerr << "[RpiCamTs] Warning: camera timestamp did not advance "
                     "after MPEG-TS time-base conversion; adjusting it by "
                     "one tick\n";
        pts = impl_->last_dts + 1;
    }

    AVPacket *packet = av_packet_alloc();
    if (packet == nullptr) {
        error_message = "failed to allocate FFmpeg packet";
        return false;
    }

    int result = av_new_packet(packet, static_cast<int>(size));
    if (result < 0) {
        error_message = "failed to allocate FFmpeg packet payload: " +
                        ffmpeg_error_string(result);
        av_packet_free(&packet);
        return false;
    }
    std::memcpy(packet->data, data, size);
    packet->stream_index = impl_->stream->index;
    packet->pts = pts;
    // The camera encoder is expected to output frames in display order without
    // B-frames. If B-frames are enabled in the future, DTS must be revisited.
    packet->dts = pts;
    packet->duration = 0;
    if (is_keyframe) {
        packet->flags |= AV_PKT_FLAG_KEY;
    }

    result = av_interleaved_write_frame(impl_->format_context, packet);
    av_packet_free(&packet);
    if (result < 0) {
        error_message = "failed to write MPEG-TS packet: " +
                        ffmpeg_error_string(result);
        return false;
    }

    impl_->last_dts = pts;
    return true;
}

void TsMuxerFFmpeg::close() {
    if (impl_ == nullptr) {
        return;
    }

    if (impl_->format_context != nullptr && impl_->header_written) {
        const int result = av_write_trailer(impl_->format_context);
        if (result < 0) {
            std::cerr << "[RpiCamTs] Warning: failed to write MPEG-TS trailer: "
                      << ffmpeg_error_string(result) << '\n';
        }
    }
    if (impl_->format_context != nullptr &&
        (impl_->format_context->oformat->flags & AVFMT_NOFILE) == 0) {
        avio_closep(&impl_->format_context->pb);
    }
    avformat_free_context(impl_->format_context);
    delete impl_;
    impl_ = nullptr;
}

bool TsMuxerFFmpeg::is_open() const {
    return impl_ != nullptr && impl_->format_context != nullptr &&
           impl_->header_written;
}

const std::string &TsMuxerFFmpeg::path() const {
    static const std::string empty_path;
    return impl_ != nullptr ? impl_->output_path : empty_path;
}
