// SPDX-License-Identifier: AGPL-3.0-only
//
// 1 ストリームの到着間隔のジッター（ゆらぎ）を推定する。
// RTP（RFC 3550）の到着間隔ジッターと同じ式で、連続する 2 フレームの「到着の間隔」と
// 「メディアタイムスタンプの間隔」の差の絶対値を、1/16 の重みで平滑化する。
// オペレーターが目標レイテンシー（ジッターバッファの上限）を回線に合わせて選べるよう、
// 実測の値を画面に出すために使う。再生の制御（PlayoutStream の目標の深さ）には使わない。
// 時刻は呼び出し側が Clock から取って渡す。テストで時刻を自由に進められるようにするため。
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

#include "vocroz/common/ports/clock.h"

namespace vocroz::transport {

/// 到着間隔のジッターの推定。
/// スレッド安全性: 1 つのオブジェクトを同時に 1 スレッドからだけ使う。
/// 通常はネットワーク受信スレッドが、SignalMonitor が受け付けたフレームごとに呼ぶ。
class JitterEstimator {
public:
    using TimePoint = common::Clock::TimePoint;

    /// フレームの到着を知らせる。
    /// 送信ミュートの世代が変わったら、タイムスタンプが連続しないので推定をやり直す
    /// （それまでの推定値は残し、次の 1 フレームを新しい起点にする）。
    /// @param timestamp  フレームのメディアタイムスタンプ（48kHz のサンプル数）
    /// @param mute_epoch フレームの送信ミュートの世代番号
    /// @param arrival    フレームの到着時刻
    void on_frame(std::uint64_t timestamp, std::uint32_t mute_epoch, TimePoint arrival) noexcept;

    /// 推定したジッター。
    /// @return まだ 2 フレーム届いていなければ std::nullopt
    [[nodiscard]] std::optional<std::chrono::microseconds> jitter() const noexcept;

private:
    struct Previous {
        std::uint64_t timestamp;
        std::uint32_t mute_epoch;
        TimePoint arrival;
    };

    std::optional<Previous> previous_;
    // 推定値（マイクロ秒）。整数の丸めで小さなゆらぎが消えないよう double で持つ。
    std::optional<double> jitter_us_;
};

}  // namespace vocroz::transport
