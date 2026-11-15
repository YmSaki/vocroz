// SPDX-License-Identifier: AGPL-3.0-only
//
// 1 つの entity の連続値（SetGain）を、送り元ごとのシーケンス番号の後勝ちで受け付けるかを決める。
// UI は画面を作り直すたびに新しい送り元（origin）を名乗り、seq を 1 から数え直す。
// 送り元を区別せずに seq だけで比べると、開き直した画面の操作が前の画面の大きな seq より古いと
// みなされて捨てられるため、比べるのは同じ送り元から届いた seq どうしに限る
// （docs/adr/0044-ui-instance-namespace.md）。
#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "vocroz/control_protocol/entities.h"

namespace vocroz::session {

/// 1 つの entity の SetGain の順序の判定。
/// スレッド安全性: コントロールスレッドからだけ使う。
class GainSequenceGate {
public:
    /// 届いた SetGain を適用してよいかを判定し、よければ送り元と seq を覚える。
    /// 最後に適用したのと同じ送り元からの、それより小さい seq だけを古い値として拒む。
    /// 別の送り元からの値は、届いた順の後勝ちで受け付ける。
    /// @param origin SetGain の送り元
    /// @param seq SetGain のシーケンス番号
    /// @return 適用してよければ true
    [[nodiscard]] bool admit(std::string_view origin, control_protocol::Sequence seq);

    /// 覚えている送り元と seq を忘れる。entity を作り直したときに使う。
    void reset();

private:
    std::optional<std::string> origin_;
    control_protocol::Sequence last_seq_ = 0;
};

}  // namespace vocroz::session
