// SPDX-License-Identifier: AGPL-3.0-only
//
// 更新を適用してよいかの判断。本番中（セッションの稼働中）は、バイナリの切り替えも再起動もしない。
#pragma once

#include "vocroz/update/semantic_version.h"

namespace vocroz::update {

/// 適用の判断に使う事実。
struct ApplyContext {
    /// いずれかのセッションが稼働中か。
    bool session_active = false;
    /// 利用者が「更新して再起動」を明示的に押したか。
    bool user_requested = false;
    /// 現在動いているバージョン。
    SemanticVersion current;
    /// 適用しようとしているバージョン。
    SemanticVersion candidate;
};

/// 適用の判断。
enum class ApplyDecision {
    /// 適用してよい。
    kApply,
    /// セッションが稼働中なので適用しない。ダウンロードと検証は済ませたまま、終了後に再び試す。
    kBlockedBySession,
    /// 利用者の明示的な操作がないので適用しない。
    kNotRequested,
    /// 現在の版より新しくないので適用しない（ダウングレード攻撃と、同じ版の再適用を防ぐ）。
    kNotNewer,
};

/// 更新を適用してよいかを判断する。
/// セッションの判定を最初に行うのは、本番中の音を止めないことが他のどの条件よりも優先されるため。
/// @return 判断の結果。純粋関数で、どのスレッドから呼んでもよい。
[[nodiscard]] ApplyDecision decide_apply(const ApplyContext& context);

}  // namespace vocroz::update
