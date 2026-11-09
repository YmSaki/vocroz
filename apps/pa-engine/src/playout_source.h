// SPDX-License-Identifier: AGPL-3.0-only
//
// 子機 1 台の受信ストリーム（transport の PlayoutStream）を、ミキサーのストリップにつなぐ
// audio-core の AudioSource にする。ジッターバッファ、デコード、ドリフト補正は PlayoutStream が
// 持ち、このクラスはオーディオスレッドの pull() を render() に渡すだけにする。
// チャンネル数（モノラルかステレオ）は PlayoutStream の設定のとおり。
#pragma once

#include <cstddef>
#include <memory>

#include "vocroz/audio_core/ports/audio_source.h"
#include "vocroz/audio_core/realtime.h"
#include "vocroz/transport/playout_stream.h"

namespace vocroz::engine {

/// 受信ストリームの AudioSource。
/// スレッド安全性: stream() の enqueue() はネットワーク受信スレッド（生産者は 1 つ）から、
/// pull() はオーディオスレッドから呼ぶ。
class PlayoutSource final : public audio_core::AudioSource {
public:
    /// @param stream 受信ストリーム（null にしない）
    explicit PlayoutSource(std::unique_ptr<transport::PlayoutStream> stream) noexcept;

    /// PlayoutStream のチャンネル数を返す。
    [[nodiscard]] std::size_t channel_count() const noexcept override;

    /// オーディオスレッドから呼び出し可能。足りないぶんは PlayoutStream が補間か無音で埋める。
    void pull(float* const* channels, std::size_t frames) noexcept VOCROZ_NONBLOCKING override;

    /// 受信ストリームを返す。
    [[nodiscard]] transport::PlayoutStream& stream() noexcept {
        return *stream_;
    }

private:
    std::unique_ptr<transport::PlayoutStream> stream_;
};

}  // namespace vocroz::engine
