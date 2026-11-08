// SPDX-License-Identifier: AGPL-3.0-only
//
// 相手からの受信の途絶を判定する死活監視。
// 音声と制御のどちらも 5 秒届かなければ途絶とみなし、LivenessListener で外側に知らせる。
// 外側（session）はこれを受けて接続状態を RECONNECTING に移す。
// 受信はネットワーク受信スレッド、判定はコントロールスレッドで行うので、両者は原子変数だけで受け渡す。
#pragma once

#include <atomic>

#include "vocroz/audio_core/realtime.h"
#include "vocroz/common/ports/clock.h"
#include "vocroz/transport/ports/liveness_listener.h"

namespace vocroz::transport {

/// 1 つの接続の死活監視。作った時刻を最後の受信とみなして監視を始める。
class LivenessMonitor {
public:
    /// @param clock    時刻の取得元
    /// @param listener 途絶と再開の知らせ先
    /// 2 つとも、このオブジェクトより長く生存させる（AGENTS.md 4.3）。
    LivenessMonitor(const common::Clock& clock, LivenessListener& listener) noexcept;

    /// 認証に成功したパケット（メディアと制御のどちらでも）を受け取ったことを記録する。
    /// 認証前のパケットでは呼ばない（偽造パケットで途絶の判定を遅らせられないようにするため）。
    /// スレッド安全性: ネットワーク受信スレッドから呼び出し可能。ヒープを確保せず、ブロックしない。
    /// オーディオスレッドから呼び出し可能。
    void note_packet_received() noexcept VOCROZ_NONBLOCKING;

    /// 途絶と再開を判定し、状態が変わったときだけ listener に知らせる。
    /// スレッド安全性: コントロールスレッドから呼ぶ（1 つのスレッドだけから呼ぶ）。
    void poll();

    /// 途絶しているとみなしていれば true を返す。
    /// スレッド安全性: poll() と同じスレッドから呼ぶ。
    [[nodiscard]] bool timed_out() const noexcept {
        return timed_out_;
    }

    /// 次に poll() を呼ぶべき時刻を返す。途絶中は、受信の再開をすぐに知るために呼び続ける。
    /// スレッド安全性: poll() と同じスレッドから呼ぶ。
    [[nodiscard]] common::Clock::TimePoint next_deadline() const noexcept;

private:
    [[nodiscard]] common::Clock::TimePoint last_received() const noexcept;

    const common::Clock& clock_;
    LivenessListener& listener_;
    // std::atomic<time_point> はロックフリーが保証されないので、刻み数で持つ。
    std::atomic<common::Clock::TimePoint::duration::rep> last_received_ticks_;
    // 途絶を知らせたときの最後の受信時刻。これより後の受信があれば再開とみなす。
    common::Clock::TimePoint timed_out_at_last_received_{};
    bool timed_out_ = false;
};

}  // namespace vocroz::transport
