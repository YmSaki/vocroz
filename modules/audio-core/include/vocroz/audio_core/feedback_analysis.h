// SPDX-License-Identifier: AGPL-3.0-only
//
// 音声グラフのフィードバックループ（発振経路）の検出。
// グラフの構造だけから決まる純粋関数で、ルーティングのステージング（session）が、適用の前に
// ループの有無と、オペレーターの承認で許可できるループか（遅延ノードを通るか）を判定し、
// ルーティング画面でループに含まれるノードを示すために使う。
#pragma once

#include <vector>

#include "vocroz/audio_core/graph_description.h"

namespace vocroz::audio_core {

/// フィードバックループの検出結果。
struct FeedbackReport {
    /// ループに含まれるノード（NodeId の昇順、重複なし）。ループがなければ空。
    std::vector<NodeId> looped_nodes;
    /// 遅延ノードを通らないループに含まれるノード（NodeId の昇順）。空でなければ、
    /// オペレーターが承認しても適用できない（遅延のないループは 1 サンプルの中で値が決まらず、
    /// 処理順を決められないうえ、ゲインが 1 を超えると即座に発振する）。
    std::vector<NodeId> undelayed_nodes;

    /// ループがあれば true を返す。
    [[nodiscard]] bool has_loops() const noexcept {
        return !looped_nodes.empty();
    }

    /// すべてのループが遅延ノードを通るなら true を返す（ループがなければ true）。
    [[nodiscard]] bool all_loops_delayed() const noexcept {
        return undelayed_nodes.empty();
    }
};

/// グラフのフィードバックループを検出する。
/// 存在しないノードを指す結線は無視する（記述の妥当性は Mixer::validate_graph() で検証する）。
/// @param description グラフの記述
/// @return 検出結果
/// スレッド安全性:
/// 純粋関数。どのスレッドからも呼び出し可能（オーディオスレッドを除く。ヒープを確保する）。
[[nodiscard]] FeedbackReport analyze_feedback(const GraphDescription& description);

}  // namespace vocroz::audio_core
