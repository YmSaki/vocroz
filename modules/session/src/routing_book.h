// SPDX-License-Identifier: AGPL-3.0-only
//
// ルーティングの状態（適用中の構成、ステージング中の構成、バスとセンドのレベル）と、その遷移の規則。
// 構成は「ステージング → 検証 → 承認 → 適用」の順にだけ変わり、ミキサーのグラフを差し替えるのは
// 適用のときだけにする。ミキサーへの反映と UI
// への通知は呼び出し側（HostCommandHandler、HostContext） が行い、ここは状態だけを持つ。
#pragma once

#include <span>
#include <string>

#include "vocroz/common/result.h"
#include "vocroz/control_protocol/entities.h"
#include "vocroz/control_protocol/states.h"

namespace vocroz::session {

/// ルーティングの状態。
/// revision は、適用中かステージング中の構成をコマンドで変えたときと、ストリップの削除で
/// 適用中の構成から参照を取り除いたときに進める。
/// スレッド安全性: コントロールスレッドからだけ使う。
class RoutingBook {
public:
    RoutingBook();

    /// UI に送るルーティング全体の状態。
    [[nodiscard]] const control_protocol::RoutingInfo& info() const noexcept {
        return info_;
    }

    /// 構成をステージングする。検証で問題があっても受け付け、理由を RoutingCheck に残す。
    /// @param config    ステージングする構成
    /// @param strip_ids 今あるチャンネルストリップの entity_id
    void stage(control_protocol::RoutingConfig config, std::span<const std::string> strip_ids);

    /// ステージング中の構成を捨てる。
    /// @return 捨てる構成がなければ INVALID_STATE
    [[nodiscard]] common::Result<void, control_protocol::ErrorCode> discard();

    /// ステージング中の構成を検証し直し、適用してよいかを判断する。状態は変えない
    /// （ステージング中の検証の結果だけは最新にする）。
    /// @param approve_feedback オペレーターがループを承認した
    /// @param strip_ids        今あるチャンネルストリップの entity_id
    /// @return 適用してよければ成功。ステージング中の構成がない、問題がある、承認の要るループを
    ///         承認していない、のいずれかなら INVALID_STATE
    [[nodiscard]] common::Result<void, control_protocol::ErrorCode> prepare_commit(
        bool approve_feedback, std::span<const std::string> strip_ids);

    /// prepare_commit() が成功した構成を適用中にする。新しいバスとセンドのレベルは
    /// ミュートで 0 dB（初期ミュート）、同じ entity_id が残るものは値を引き継ぐ。
    void commit();

    /// バスかセンドのレベルを返す。なければ nullptr。
    [[nodiscard]] control_protocol::RouteLevel* find_level(const std::string& entity_id) noexcept;

    /// ストリップの削除を反映する。適用中の構成からそのストリップを参照するセンドと手動ディレイを
    /// 取り除き、ステージング中の構成は検証し直す（参照が切れたことを UI に示すため）。
    /// @param strip_id  削除したストリップ
    /// @param strip_ids 削除後に残っているチャンネルストリップの entity_id
    /// @return 適用中の構成が変わり、ミキサーへ適用し直す必要があれば true
    bool forget_strip(const std::string& strip_id, std::span<const std::string> strip_ids);

private:
    control_protocol::RoutingInfo info_;
};

}  // namespace vocroz::session
