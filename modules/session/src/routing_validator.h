// SPDX-License-Identifier: AGPL-3.0-only
//
// ステージングしたルーティングの構成の検証。
// 構成は UI が組み立てた外部入力なので、参照の解決、つなげる組み合わせ、値の範囲、ループを
// 適用の前にここで確かめ、ルーティング画面に理由を返す。ミキサーのグラフの細部（フェーダーの
// 容量など）はコンポジションルートが Mixer::validate_graph() で確かめる。
#pragma once

#include <span>
#include <string>

#include "vocroz/control_protocol/entities.h"

namespace vocroz::session {

/// 構成を検証する。
/// ノードはストリップ（strip_ids）、Master、構成のバス、出力、遅延ノードと、暗黙のメインの出力。
/// ストリップから Master と、Master からメインの出力への結線は暗黙にあるものとして扱う。
/// @param config    検証する構成
/// @param strip_ids 今あるチャンネルストリップの entity_id
/// @return 検証の結果。問題があれば最初に見つかった 1 つを返す
/// スレッド安全性: 純粋関数。コントロールスレッドから呼ぶ。
[[nodiscard]] control_protocol::RoutingCheck check_routing(
    const control_protocol::RoutingConfig& config, std::span<const std::string> strip_ids);

}  // namespace vocroz::session
