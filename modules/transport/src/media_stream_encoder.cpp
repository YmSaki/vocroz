// SPDX-License-Identifier: AGPL-3.0-only
//
// MediaStreamEncoder の実装。

#include "vocroz/transport/media_stream_encoder.h"

#include <utility>

namespace vocroz::transport {

MediaStreamEncoder::MediaStreamEncoder(OpusFrameEncoder encoder, MediaFrameSink& sink) noexcept
    : encoder_(std::move(encoder)), sink_(sink) {}

bool MediaStreamEncoder::set_params(const StreamSendParams& params) noexcept {
    return params_.try_push(params);
}

void MediaStreamEncoder::apply_pending_params() noexcept VOCROZ_NONBLOCKING {
    bool changed = false;
    while (const auto next = params_.try_pop()) {
        current_ = *next;
        changed = true;
    }
    if (changed) {
        ramp_.set_target(current_.muted ? 0.0F : current_.gain);
    }
}

void MediaStreamEncoder::push(std::span<const float> samples) noexcept VOCROZ_NONBLOCKING {
    apply_pending_params();
    for (const float sample : samples) {
        frame_[filled_] = sample * ramp_.next();
        ++filled_;
        if (filled_ == encoder_.frame_samples()) {
            finish_frame();
            filled_ = 0;
        }
    }
}

void MediaStreamEncoder::finish_frame() noexcept VOCROZ_NONBLOCKING {
    const std::uint64_t timestamp = next_timestamp_;
    // 送らないフレームでもタイムスタンプは実時間どおりに進める。受信側のジッターバッファは
    // タイムスタンプの飛びを無音の区間として扱える。
    next_timestamp_ += encoder_.frame_samples();
    if (current_.muted && ramp_.is_settled()) {
        // ミュートのランプが終わったら送らない。返送のミュート中は不要な通信をしない。
        return;
    }
    const auto pcm = std::span<const float>(frame_).first(encoder_.frame_samples());
    const auto encoded = encoder_.encode(pcm, encoded_.payload);
    if (!encoded) {
        dropped_frames_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    encoded_.timestamp = timestamp;
    encoded_.mute_epoch = current_.mute_epoch;
    encoded_.payload_size = static_cast<std::uint16_t>(encoded.value());
    if (!sink_.push_frame(encoded_)) {
        dropped_frames_.fetch_add(1, std::memory_order_relaxed);
    }
}

}  // namespace vocroz::transport
