// SPDX-License-Identifier: AGPL-3.0-only
//
// ルーティングの構成（control-protocol の RoutingConfig）とストリップの一覧から、audio-core の
// 音声グラフ（GraphDescription）を組み立てる（docs/adr/0026-routing-staging.md、
// docs/adr/0028-engine-assembly.md）。
//
// グラフの形:
//   ソース → ストリップ（手動ディレイ、プリフェーダーのメーター、フェーダー）→ Master
//   Master → 安全リミッター → メインの出力（デバイスのチャンネル 0 と 1、最終出力のメーター）
//   バス、遅延ノード、追加の出力（それぞれ安全リミッターを通す）と、センドの結線
//   子機への返送（Minus-Me）: Master へ入る信号のうち、その子機の声を含まないもの → 安全リミッター
//   → 返送タップ（docs/adr/0036-return-monitor.md）
// ストリップから Master と、Master からメインの出力は暗黙の結線として必ず加える。
// フェーダーとメーターの識別子は EngineMixer が entity ごとに割り当て、ここでは使うだけにする。
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "vocroz/audio_core/graph_description.h"
#include "vocroz/audio_core/ports/audio_sink.h"
#include "vocroz/audio_core/ports/audio_source.h"
#include "vocroz/control_protocol/entities.h"

namespace vocroz::engine {

/// 入力チャンネルストリップ 1 本の割り当て。
struct StripAssignment {
    /// ストリップの entity_id。
    std::string strip_id;
    audio_core::FaderId fader;
    /// プリフェーダーのメーター。
    audio_core::MeterId meter;
    /// 子機の受信ストリーム。まだ受け付けていなければ null（ストリップだけを置いて無音にする）。
    std::shared_ptr<audio_core::AudioSource> source;
};

/// 子機 1 台への返送（Minus-Me）の割り当て。返送は子機 1 台に 1 本で、子機が複数の入力を
/// 送っていれば、そのすべてのストリップを返送から抜く（docs/adr/0036-return-monitor.md）。
struct ReturnAssignment {
    /// 返送を区別するキー（その子機との接続）。
    std::uint64_t key = 0;
    /// その子機のストリップの entity_id。見つからないものは無視する。
    std::vector<std::string> own_strip_ids;
    /// 返送の渡し先（null にしない）。
    std::shared_ptr<audio_core::AudioSink> sink;
};

/// グラフを組み立てるのに使う、フェーダーとメーターの割り当て。
struct MixerAssignments {
    audio_core::FaderId master_fader;
    /// Master の最終出力（リミッターの後）のメーター。
    audio_core::MeterId master_meter;
    /// ストリップ。この順にノードを並べる（並び順は処理順に影響しない）。
    std::vector<StripAssignment> strips;
    /// 子機への返送。
    std::vector<ReturnAssignment> returns;
    /// バスとセンドの entity_id ごとのフェーダー。
    std::map<std::string, audio_core::FaderId, std::less<>> route_faders;
    /// バスの entity_id ごとのポストフェーダーのメーター。
    std::map<std::string, audio_core::MeterId, std::less<>> bus_meters;
};

/// 組み立てたグラフ。
struct BuiltMixerGraph {
    audio_core::GraphDescription graph;
    /// 送り元か送り先が見つからなかったため外したセンドの entity_id。
    /// session はストリップの削除のあとで構成から参照を取り除くので、その間の組み立てで起きる。
    /// 呼び出し側はこれらのセンドのフェーダーをミュートする（フェイルセーフ）。
    std::vector<std::string> dropped_sends;
};

/// グラフを組み立てる。
/// 構成は session が検証済みとして扱うが、存在しないノードを指すセンドは外し、
/// 存在しないストリップの手動ディレイは無視する（音の経路を増やさない側に倒す）。
/// フェーダーの割り当てがないバスとセンドも、同じ理由で外す。
/// @param assignments       フェーダーとメーターの割り当て
/// @param master_entity_id  Master の entity_id（センドの送り元と送り先に使われる）
/// @param config            ルーティングの構成
/// @param feedback_approved 遅延ノードを通るループをオペレーターが承認した
/// @return グラフと、外したセンド
/// スレッド安全性: 純粋関数。コントロールスレッドから呼ぶ。
[[nodiscard]] BuiltMixerGraph build_mixer_graph(const MixerAssignments& assignments,
                                                std::string_view master_entity_id,
                                                const control_protocol::RoutingConfig& config,
                                                bool feedback_approved);

}  // namespace vocroz::engine
