// SPDX-License-Identifier: AGPL-3.0-only
//
// 信頼制御層の再送タイムアウトを、往復時間（RTT）の測定値から決める。
// 計算は RFC 6298 の平滑化 RTT と RTT の変動に従い、上下限だけを低遅延向けに変えている
// （下限と上限の理由は transport_constants.h）。
#pragma once

#include "vocroz/common/ports/clock.h"

namespace vocroz::transport {

/// RTT の測定値から再送タイムアウトを求める。
/// スレッド安全性: 1 つのオブジェクトは 1 つのスレッド（コントロールスレッド）から使う。
class RttEstimator {
public:
    /// 時間の型。Clock と同じ分解能を使う。
    using Duration = common::Clock::TimePoint::duration;

    /// RTT の測定値を 1 つ加える。再送したメッセージの ACK からは測らない（Karn のアルゴリズム。
    /// どの送信への ACK かが区別できず、RTT を誤って短く見積もるため）。
    /// @param rtt 送信から ACK の受信までの時間
    void add_sample(Duration rtt) noexcept;

    /// 新しく送るメッセージの再送タイムアウトを返す。測定値がなければ初期値。
    [[nodiscard]] Duration retransmit_timeout() const noexcept;

private:
    Duration smoothed_rtt_{};
    Duration rtt_variation_{};
    bool has_sample_ = false;
};

}  // namespace vocroz::transport
