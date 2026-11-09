// SPDX-License-Identifier: AGPL-3.0-only
//
// CaptureProcessor の実装。

#include "capture_processor.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace vocroz::client {

CaptureProcessor::CaptureProcessor(transport::OpusFrameEncoder encoder, PacketSender& sender,
                                   std::size_t lane, std::uint16_t stream_id) noexcept
    : encoder_(std::move(encoder)), sender_(sender), lane_(lane), stream_id_(stream_id) {}

bool CaptureProcessor::set_params(const SendParams& params) noexcept {
    return params_.try_push(params);
}

void CaptureProcessor::restart(const SendParams& params) noexcept {
    while (params_.try_pop()) {
    }
    current_ = params;
    ramp_.jump_to(0.0F);
    ramp_.set_target(current_.muted ? 0.0F : current_.gain);
    filled_ = 0;
}

float CaptureProcessor::take_input_peak() noexcept {
    return input_peak_.exchange(0.0F, std::memory_order_acq_rel);
}

void CaptureProcessor::apply_pending_params() noexcept VOCROZ_NONBLOCKING {
    bool changed = false;
    while (const auto next = params_.try_pop()) {
        current_ = *next;
        changed = true;
    }
    if (changed) {
        ramp_.set_target(current_.muted ? 0.0F : current_.gain);
    }
}

void CaptureProcessor::on_captured(audio_core::ConstInterleavedBuffer in) noexcept
    VOCROZ_NONBLOCKING {
    apply_pending_params();
    const std::size_t frames = in.frames();
    const std::size_t channels = encoder_.channels();
    // 入力が 1 チャンネルなら、右にも左と同じサンプルを使う。
    const std::size_t right_offset = in.channels >= 2 ? 1 : 0;
    float peak = 0.0F;
    for (std::size_t i = 0; i < frames; ++i) {
        const float left = in.samples[i * in.channels];
        const float gain = ramp_.next();
        peak = std::max(peak, std::fabs(left));
        if (channels == 1) {
            frame_[filled_] = left * gain;
        } else {
            const float right = in.samples[(i * in.channels) + right_offset];
            peak = std::max(peak, std::fabs(right));
            frame_[filled_ * 2] = left * gain;
            frame_[(filled_ * 2) + 1] = right * gain;
        }
        ++filled_;
        if (filled_ == encoder_.frame_samples()) {
            finish_frame();
            filled_ = 0;
        }
    }
    // 読み手が 0 に戻すまでの最大値を保つ。書き手はオーディオスレッドだけなので、比較と書き込みの
    // 間に読み手が 0 に戻しても、このブロックのピークが次の期間に入るだけで値は失われない。
    if (peak > input_peak_.load(std::memory_order_relaxed)) {
        input_peak_.store(peak, std::memory_order_release);
    }
}

void CaptureProcessor::finish_frame() noexcept VOCROZ_NONBLOCKING {
    const std::uint64_t timestamp = next_timestamp_;
    // 送らないフレームでもタイムスタンプは実時間どおりに進める。受信側のジッターバッファは
    // タイムスタンプの飛びを無音の区間として扱える。
    next_timestamp_ += encoder_.frame_samples();
    if (current_.muted && ramp_.is_settled()) {
        // ミュートのランプが終わったら送らない（出演者の音声を PC の外へ出さない）。
        return;
    }
    const auto pcm =
        std::span<const float>(frame_).first(encoder_.frame_samples() * encoder_.channels());
    const auto encoded = encoder_.encode(pcm, encoded_.bytes);
    if (!encoded) {
        dropped_frames_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    encoded_.stream_id = stream_id_;
    encoded_.timestamp = timestamp;
    encoded_.mute_epoch = current_.mute_epoch;
    encoded_.size = encoded.value();
    if (!sender_.push_frame(lane_, encoded_)) {
        dropped_frames_.fetch_add(1, std::memory_order_relaxed);
    }
}

}  // namespace vocroz::client
