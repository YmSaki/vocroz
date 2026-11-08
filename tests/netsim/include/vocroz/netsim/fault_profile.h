// SPDX-License-Identifier: AGPL-3.0-only
//
// 1 方向の経路に注入する障害の設定（遅延、ジッター、欠損、重複、順序の入れ替え、改ざん）と、
// その文字列表現のパース。
// 結合テスト、UDP 障害注入プロキシ、遅延計測ツールが同じ設定と同じ書式を使い、
// CI で再現した条件をそのまま実機の確認に持ち出せるようにする。
#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

#include "vocroz/common/result.h"

namespace vocroz::netsim {

/// 1 方向の経路の障害。
/// 既定値は障害なし（遅延 0、欠損 0）。
struct FaultProfile {
    /// すべてのパケットに加える固定の遅延（片道）。
    std::chrono::microseconds delay{0};
    /// 固定の遅延に加える揺らぎの幅。[0, jitter) の一様分布で加える。
    /// パケットの間隔より大きいと、揺らぎだけでも順序が入れ替わる。
    std::chrono::microseconds jitter{0};
    /// パケットを捨てる確率（0〜1）。
    double loss = 0.0;
    /// パケットを 2 つ届ける確率（0〜1）。2 つ目は reorder_delay だけ遅れて届く。
    double duplicate = 0.0;
    /// パケットを reorder_delay だけ余分に遅らせ、後続のパケットに追い越させる確率（0〜1）。
    double reorder = 0.0;
    /// 順序の入れ替えと重複の 2 つ目に加える遅延。
    std::chrono::microseconds reorder_delay{10'000};
    /// パケットの 1 ビットを反転させる確率（0〜1）。AEAD による改ざんの検出を確かめる。
    double corrupt = 0.0;
    /// 乱数のシード。同じシードと同じ送信の順序なら、同じ障害の列になる。
    std::uint64_t seed = 1;
};

/// 障害の設定の文字列をパースする。
/// 書式は `key=value` をカンマで区切ったもの（例: `delay=20ms,jitter=5ms,loss=0.02,seed=7`）。
/// 時間の単位は `us`、`ms`、`s`。確率は 0〜1 の小数。書かなかった項目は既定値のまま。
/// キーは delay、jitter、loss、duplicate、reorder、reorder_delay、corrupt、seed。
/// 空文字列は障害なしを表す。
/// @param text 設定の文字列
/// @return パースした設定。未知のキー、値の書式の誤り、範囲外の確率なら、原因を書いたメッセージ
/// スレッド安全性: 純粋関数で、どのスレッドからも呼び出し可能。
[[nodiscard]] common::Result<FaultProfile, std::string> parse_fault_profile(std::string_view text);

/// 障害の設定を parse_fault_profile() で読み戻せる文字列にする。
/// ログに残して、同じ条件を再現できるようにするために使う。
/// @param profile 設定
/// @return すべての項目を書いた文字列
[[nodiscard]] std::string format_fault_profile(const FaultProfile& profile);

}  // namespace vocroz::netsim
