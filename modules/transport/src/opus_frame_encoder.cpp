// SPDX-License-Identifier: AGPL-3.0-only
//
// opus_frame_encoder.h の実装。
// libopus を直接呼ぶのはこのファイルと opus_frame_decoder.cpp だけにする。

#include "vocroz/transport/opus_frame_encoder.h"

#include <algorithm>

#include <opus.h>

namespace vocroz::transport {

void OpusFrameEncoder::Deleter::operator()(::OpusEncoder* encoder) const noexcept {
    opus_encoder_destroy(encoder);
}

namespace {

constexpr int kChannels = 1;

// 低遅延モードの設定を適用する。
// どれか 1 つでも拒否されたら、意図しない設定で動かさないよう生成を失敗させる。
bool configure(::OpusEncoder* encoder, const OpusEncoderConfig& config) noexcept {
    // DTX は使わない。
    // 無音でもフレームを送り続け、ミュートによる停止（PAUSED）と回線の途絶（NO_SIGNAL）
    // をフレームの有無で区別できるようにするため。
    // インバンド FEC は低遅延モードの CELT では効かないので明示的に切る。
    return opus_encoder_ctl(encoder, OPUS_SET_BITRATE(config.bitrate_bps)) == OPUS_OK &&
           opus_encoder_ctl(encoder, OPUS_SET_DTX(0)) == OPUS_OK &&
           opus_encoder_ctl(encoder, OPUS_SET_INBAND_FEC(0)) == OPUS_OK &&
           opus_encoder_ctl(encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC)) == OPUS_OK;
}

}  // namespace

common::Result<OpusFrameEncoder, CodecError> OpusFrameEncoder::create(
    const OpusEncoderConfig& config) noexcept {
    int error = OPUS_OK;
    Handle encoder{opus_encoder_create(static_cast<opus_int32>(kSampleRate), kChannels,
                                       OPUS_APPLICATION_RESTRICTED_LOWDELAY, &error)};
    if (error != OPUS_OK || encoder == nullptr || !configure(encoder.get(), config)) {
        return common::make_unexpected(CodecError::kCreateFailed);
    }
    opus_int32 lookahead = 0;
    if (opus_encoder_ctl(encoder.get(), OPUS_GET_LOOKAHEAD(&lookahead)) != OPUS_OK ||
        lookahead < 0) {
        return common::make_unexpected(CodecError::kCreateFailed);
    }
    return OpusFrameEncoder{std::move(encoder), transport::frame_samples(config.frame_duration),
                            static_cast<std::size_t>(lookahead)};
}

OpusFrameEncoder::OpusFrameEncoder(Handle encoder, std::size_t frame_samples,
                                   std::size_t lookahead_samples) noexcept
    : encoder_(std::move(encoder)),
      frame_samples_(frame_samples),
      lookahead_samples_(lookahead_samples) {}

OpusFrameEncoder::OpusFrameEncoder(OpusFrameEncoder&&) noexcept = default;
OpusFrameEncoder& OpusFrameEncoder::operator=(OpusFrameEncoder&&) noexcept = default;
OpusFrameEncoder::~OpusFrameEncoder() = default;

common::Result<std::size_t, CodecError> OpusFrameEncoder::encode(
    std::span<const float> pcm, std::span<std::uint8_t> out) noexcept {
    if (pcm.size() != frame_samples_) {
        return common::make_unexpected(CodecError::kInvalidFrameSize);
    }
    const std::size_t capacity = std::min(out.size(), kMaxOpusPayloadBytes);
    if (capacity == 0) {
        return common::make_unexpected(CodecError::kBufferTooSmall);
    }
    const opus_int32 bytes =
        opus_encode_float(encoder_.get(), pcm.data(), static_cast<int>(pcm.size()), out.data(),
                          static_cast<opus_int32>(capacity));
    if (bytes <= 0) {
        return common::make_unexpected(CodecError::kEncodeFailed);
    }
    return static_cast<std::size_t>(bytes);
}

}  // namespace vocroz::transport
