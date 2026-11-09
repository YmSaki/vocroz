// SPDX-License-Identifier: AGPL-3.0-only
//
// 子機の時計での MIDI の時刻を、親機の時計の時刻に写す。
// 子機と親機の時計の差は直接は分からないので、「受信時刻 − 子機の時刻」の直近の最小値
// （片道の遅延が最も小さかったとき）を差とみなす（docs/adr/0029-midi-and-vsti.md）。
// 最小値を使うと、写した時刻は受信時刻より後にならない。ネットワークの揺れで詰まったり
// 開いたりしたイベントの間隔は、親機の MidiInputQueue の schedule_ahead_samples の範囲で元に戻る。
// 子機の時計の進み方のずれ（クロックドリフト）と経路の変化は、窓ごとに最小値を取り直して追う。
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

#include "vocroz/common/ports/clock.h"

namespace vocroz::session {

/// 子機 1 台ぶんの時刻の写し方。
/// スレッド安全性: 1 つのスレッド（MIDI を受け取るスレッド）だけが使う。
class MidiTimeline {
public:
    /// 子機の時刻を親機の時刻に写す。
    /// @param sender_time_us 子機の時計での時刻（マイクロ秒、桁あふれで一周する）
    /// @param arrival 親機がメッセージを受け取った時刻
    /// @return 親機の時計での時刻。arrival より後にはならない
    [[nodiscard]] common::Clock::TimePoint map(std::uint32_t sender_time_us,
                                               common::Clock::TimePoint arrival) noexcept;

    /// 状態を捨てて最初からやり直す。子機の再接続（時計が変わりうる）で呼ぶ。
    void reset() noexcept;

private:
    using Microseconds = std::chrono::microseconds;

    // 子機の時刻を一周ぶんの桁あふれを補って 64 ビットに伸ばす。
    [[nodiscard]] std::int64_t unwrap(std::uint32_t sender_time_us) noexcept;

    std::optional<std::uint32_t> last_sender_;
    std::int64_t wraps_ = 0;
    // 窓ごとの「受信時刻 − 子機の時刻」の最小値。いまの窓と 1 つ前の窓の小さい方を使い、
    // 窓が切り替わった直後でも最小値が急に大きくならないようにする。
    std::optional<Microseconds> current_min_;
    std::optional<Microseconds> previous_min_;
    std::optional<common::Clock::TimePoint> window_start_;
};

}  // namespace vocroz::session
