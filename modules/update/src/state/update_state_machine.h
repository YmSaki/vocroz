// SPDX-License-Identifier: AGPL-3.0-only
//
// 更新トランザクションの状態遷移表。どの段階の失敗でロールバックが要るかを、ここだけで決める。
// 遷移関数は次の状態を返すだけで、副作用（展開、削除、切り替え）は UpdateApplier が実行する。
#pragma once

#include <cstdint>

namespace vocroz::update::state {

/// 更新トランザクションの状態。
enum class UpdateState : std::uint8_t {
    /// 開始前。
    kIdle,
    /// パッケージのサイズと SHA-256 をマニフェストと照合している。まだ何も変えていない。
    kVerifyingPackage,
    /// 設定を退避している。まだ何も変えていない。
    kBackingUp,
    /// パッケージを新しいバージョン別フォルダに展開している。
    kExtracting,
    /// 展開した実行ファイルの Authenticode を検証している。
    kVerifyingSignature,
    /// 新しい版の起動確認をしている。
    kCheckingHealth,
    /// ポインターファイルを新しい版に切り替えている。
    kSwitching,
    /// 切り替えが完了した（終端）。
    kCommitted,
    /// 展開したフォルダの削除と設定の復元をしている。
    kRollingBack,
    /// ロールバックが完了し、旧版のまま動く状態に戻った（終端）。
    kRolledBack,
    /// 何も変えずに中止した（終端）。
    kAborted,
};

/// 状態を進めるイベント。
enum class UpdateEvent : std::uint8_t {
    /// トランザクションを始める。
    kStart,
    /// 現在の段階が成功した。
    kStepSucceeded,
    /// 現在の段階が失敗した。
    kStepFailed,
    /// ロールバックの処理を終えた。
    kRollbackFinished,
};

/// 次の状態を返す。表にない組み合わせでは状態を変えない（呼び出し側の誤りで先に進まないため）。
/// 失敗したとき、インストール先をまだ変えていない段階なら kAborted、変えた後なら kRollingBack
/// に進む。
/// @return 次の状態。純粋関数で、どのスレッドから呼んでもよい。
[[nodiscard]] UpdateState next_state(UpdateState state, UpdateEvent event) noexcept;

/// 終端の状態（kCommitted、kRolledBack、kAborted）なら true。
[[nodiscard]] bool is_terminal(UpdateState state) noexcept;

}  // namespace vocroz::update::state
