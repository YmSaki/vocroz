// SPDX-License-Identifier: AGPL-3.0-only
//
// 子機 1 台の受信ストリーム（transport の PlayoutStream）を、ミキサーのストリップにつなぐ
// audio-core の AudioSource にする。ジッターバッファ、デコード、ドリフト補正は PlayoutStream が
// 持ち、このクラスはオーディオスレッドの pull() を render() に渡すだけにする。
// チャンネル数（モノラルかステレオ）は PlayoutStream の設定のとおり。
//
// ストリップに出す回線の実測値（ジッター）の推定もここで持つ。推定はフレームごとに受信スレッドで
// 行い、画面へ送るコントロールスレッドは約 1 Hz で読むだけなので、推定値をアトミック変数に置いて
// 渡す（docs/adr/0037-latency-target-and-link-stats.md）。
#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

#include "vocroz/audio_core/ports/audio_source.h"
#include "vocroz/audio_core/realtime.h"
#include "vocroz/common/ports/clock.h"
#include "vocroz/transport/jitter_estimator.h"
#include "vocroz/transport/media_frame.h"
#include "vocroz/transport/playout_stream.h"

namespace vocroz::engine {

/// 受信ストリームの AudioSource。
/// スレッド安全性: receive() はネットワーク受信スレッド（生産者は 1 つ）から、
/// pull() はオーディオスレッドから呼ぶ。jitter() と stream().buffer_delay() はどのスレッドからも
/// 呼び出し可能。
class PlayoutSource final : public audio_core::AudioSource {
public:
    /// @param stream 受信ストリーム（null にしない）
    explicit PlayoutSource(std::unique_ptr<transport::PlayoutStream> stream) noexcept;

    /// PlayoutStream のチャンネル数を返す。
    [[nodiscard]] std::size_t channel_count() const noexcept override;

    /// オーディオスレッドから呼び出し可能。足りないぶんは PlayoutStream が補間か無音で埋める。
    void pull(float* const* channels, std::size_t frames) noexcept VOCROZ_NONBLOCKING override;

    /// 信号状態の判定が受け付けたフレームのジッターを推定してから、ジッターバッファへ渡す。
    /// @param frame   受け付けたフレーム
    /// @param arrival フレームの到着時刻
    /// @return ジッターバッファへ渡せたら true。キューが満杯なら捨てて false
    /// （ジッターの推定には数える。到着の揺れは捨てたかどうかに関係しないため）
    /// スレッド安全性: ネットワーク受信スレッドから呼ぶ。ヒープを確保せず、ブロックしない。
    bool receive(const transport::MediaFrame& frame, common::Clock::TimePoint arrival) noexcept;

    /// 推定したジッターを返す。
    /// @return まだ 2 フレーム届いていなければ std::nullopt
    /// スレッド安全性: どのスレッドからも呼び出し可能。
    [[nodiscard]] std::optional<std::chrono::microseconds> jitter() const noexcept;

    /// 受信ストリームを返す。
    [[nodiscard]] transport::PlayoutStream& stream() noexcept {
        return *stream_;
    }

private:
    // 推定がまだないことを表す jitter_us_ の値。
    static constexpr std::int64_t kNoJitter = -1;

    std::unique_ptr<transport::PlayoutStream> stream_;
    // 受信スレッドだけが使う。
    transport::JitterEstimator estimator_;
    // estimator_ の推定値（µs）。受信スレッドが置き、コントロールスレッドが読む。
    std::atomic<std::int64_t> jitter_us_{kNoJitter};
};

}  // namespace vocroz::engine
