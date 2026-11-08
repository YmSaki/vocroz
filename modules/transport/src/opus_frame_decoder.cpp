// SPDX-License-Identifier: AGPL-3.0-only
//
// opus_frame_decoder.h の実装。

#include "vocroz/transport/opus_frame_decoder.h"

#include <opus.h>

#include "vocroz/audio_core/realtime.h"
#include "vocroz/transport/constants.h"

namespace vocroz::transport {

void OpusFrameDecoder::Deleter::operator()(::OpusDecoder* decoder) const noexcept {
    opus_decoder_destroy(decoder);
}

namespace {

constexpr int kChannels = 1;
// PLC で補間できる長さの単位。
// Opus のフレーム長の最小値（2.5ms）の整数倍でなければ libopus が拒否する。
constexpr std::size_t kPlcGranularitySamples = 120;

}  // namespace

common::Result<OpusFrameDecoder, CodecError> OpusFrameDecoder::create() noexcept {
    int error = OPUS_OK;
    Handle decoder{opus_decoder_create(static_cast<opus_int32>(kSampleRate), kChannels, &error)};
    if (error != OPUS_OK || decoder == nullptr) {
        return common::make_unexpected(CodecError::kCreateFailed);
    }
    return OpusFrameDecoder{std::move(decoder)};
}

OpusFrameDecoder::OpusFrameDecoder(Handle decoder) noexcept : decoder_(std::move(decoder)) {}

OpusFrameDecoder::OpusFrameDecoder(OpusFrameDecoder&&) noexcept = default;
OpusFrameDecoder& OpusFrameDecoder::operator=(OpusFrameDecoder&&) noexcept = default;
OpusFrameDecoder::~OpusFrameDecoder() = default;

common::Result<std::size_t, CodecError> OpusFrameDecoder::decode(
    std::span<const std::uint8_t> payload, std::span<float> out) noexcept VOCROZ_NONBLOCKING {
    if (payload.empty()) {
        // 空のペイロードを渡すと libopus は PLC として扱う。
        // 受信したパケットの欠陥として区別する。
        return common::make_unexpected(CodecError::kInvalidPacket);
    }
    const int samples =
        opus_decode_float(decoder_.get(), payload.data(), static_cast<opus_int32>(payload.size()),
                          out.data(), static_cast<int>(out.size()), 0);
    if (samples <= 0) {
        return common::make_unexpected(CodecError::kInvalidPacket);
    }
    return static_cast<std::size_t>(samples);
}

common::Result<std::size_t, CodecError> OpusFrameDecoder::conceal(std::span<float> out) noexcept
    VOCROZ_NONBLOCKING {
    if (out.empty() || out.size() % kPlcGranularitySamples != 0) {
        return common::make_unexpected(CodecError::kInvalidFrameSize);
    }
    const int samples =
        opus_decode_float(decoder_.get(), nullptr, 0, out.data(), static_cast<int>(out.size()), 0);
    if (samples <= 0) {
        return common::make_unexpected(CodecError::kInvalidPacket);
    }
    return static_cast<std::size_t>(samples);
}

void OpusFrameDecoder::reset() noexcept VOCROZ_NONBLOCKING {
    // OPUS_RESET_STATE は確保済みの状態を初期化するだけで、失敗しない。
    static_cast<void>(opus_decoder_ctl(decoder_.get(), OPUS_RESET_STATE));
}

}  // namespace vocroz::transport
