// SPDX-License-Identifier: AGPL-3.0-only
//
// プラグイン遅延補正（PDC）の計算結果。
// 遅延を報告するプラグインを通る経路と、通らない経路が同じノードで合流すると、同じ音が時間差で
// 重なってコムフィルターになる。そのため、合流する結線のうち遅延の少ない側にだけ遅延を足してそろえる。
// 合流しない経路（別の出力へ向かう経路）には足さず、低遅延を保つ（docs/adr/0024-plugin-node-and-pdc.md）。
#pragma once

#include <cstddef>
#include <vector>

#include "vocroz/audio_core/graph_description.h"
#include "vocroz/common/result.h"

namespace vocroz::audio_core {

/// グラフ全体の遅延と、結線ごとの補正。
/// 遅延に数えるのはプラグインが報告する処理の遅延だけで、ストリップの手動ディレイ（同期のための
/// 意図した遅延）と遅延ノード（演出の遅延）は数えない。数えると PDC
/// が意図した時間差を打ち消すため。
struct LatencyReport {
    /// ノードごとの、ソースからそのノードの出力までの遅延（サンプル）。添字は
    /// GraphDescription::nodes。
    /// 出力ノードの値が、その出力で聞こえる音のプラグインによる遅延になる。
    std::vector<std::size_t> node_latency_samples;
    /// 結線ごとの、補正のために足す遅延（サンプル）。添字は GraphDescription::connections。
    /// 遅延ノードへ入る結線（フィードバック経路）は補正しないので 0。
    std::vector<std::size_t> compensation_samples;
};

/// グラフの記述から PDC を計算する。Mixer が適用時に使うのと同じ規則で、画面にグラフ全体の遅延を
/// 示すため（ステージング中の確認）に使う。フェーダーとメーターの容量は検査しない。
/// @param description グラフの記述
/// @return 遅延と補正。記述が不正なら GraphError（遅延が上限を超えるなら kLatencyOutOfRange）
/// スレッド安全性: 純粋関数。コントロールスレッドから呼ぶ。
[[nodiscard]] common::Result<LatencyReport, GraphError> analyze_latency(
    const GraphDescription& description);

}  // namespace vocroz::audio_core
