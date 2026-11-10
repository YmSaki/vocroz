// SPDX-License-Identifier: AGPL-3.0-only
//
// ルーティングの構成（control-protocol の RoutingConfig）とストリップの一覧から、audio-core の
// 音声グラフ（GraphDescription）を組み立てる（docs/adr/0026-routing-staging.md、
// docs/adr/0028-engine-assembly.md）。
//
// グラフの形:
//   ソース →（挿入）→ ストリップ（手動ディレイ、プリフェーダーのメーター、フェーダー）→ Master
//   Master →（挿入）→ 安全リミッター → メインの出力（デバイスのチャンネル 0 と
//   1、最終出力のメーター） バス →（挿入）→
//   センド。遅延ノード、追加の出力（それぞれ安全リミッターを通す）と、センドの結線
//   挿入は挿す先ごとに構成の順で直列につなぎ、読み込みの済んでいない挿入があれば、その挿す先を
//   通る経路を切る（docs/adr/0042-plugin-insert.md）
//   子機への返送（Minus-Me）: Master へ入る信号のうち、その子機の声を含まないもの → 安全リミッター
//   → 返送タップ（docs/adr/0036-return-monitor.md）
//   SRTP 外部出力: 送り出すバス（Master かバス）のポストフェーダー → 安全リミッター
//   → ストリームタップ（docs/adr/0040-srtp-external-output.md）
// ストリップから Master と、Master からメインの出力は暗黙の結線として必ず加える。
// フェーダーとメーターの識別子は EngineMixer が entity ごとに割り当て、ここでは使うだけにする。
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "vocroz/audio_core/graph_description.h"
#include "vocroz/audio_core/ports/audio_sink.h"
#include "vocroz/audio_core/ports/audio_source.h"
#include "vocroz/audio_core/ports/audio_stream_sink.h"
#include "vocroz/audio_core/ports/midi_source.h"
#include "vocroz/control_protocol/entities.h"
#include "vocroz/session/ports/mixer_control.h"

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

/// MIDI ソース 1 つ（MIDI デバイス 1 台、子機 1 台）の割り当て。経路（midi_connections）は
/// MIDI の送り先（プラグインノード）がグラフに入ってから加える。経路がなくてもグラフは
/// ブロックごとにソースから取り出すので、キューはあふれない（docs/adr/0029-midi-and-vsti.md）。
struct MidiSourceAssignment {
    /// ソースを区別するキー（"device:<ポートの ID>"、"strip:<ストリップの entity_id>"）。
    std::string key;
    /// グラフ内の識別子。同じソースを付け替えても変えない（経路ごとの発音中のノートを
    /// 引き継ぐため）。
    audio_core::MidiSourceId id;
    /// イベントの取り出し先（null にしない）。
    std::shared_ptr<audio_core::MidiSource> source;
};

/// 子機の MIDI（ストリップのキュー）の MidiSourceAssignment::key を返す。
/// @param strip_id ストリップの entity_id
[[nodiscard]] std::string strip_midi_key(std::string_view strip_id);

/// MIDI デバイスの MidiSourceAssignment::key を返す。
/// @param device_id MIDI 入力ポートの ID（MidiDeviceInfo::id）
[[nodiscard]] std::string device_midi_key(std::string_view device_id);

/// SRTP 外部出力のストリームタップの割り当て（docs/adr/0040-srtp-external-output.md）。
struct ExternalStreamAssignment {
    /// 送り出すバスの entity_id（Master か、構成のバス）。見つからなければタップを置かない。
    std::string source_id;
    /// 送り出す音の渡し先（null にしない）。
    std::shared_ptr<audio_core::AudioStreamSink> sink;
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
    /// MIDI ソース。
    std::vector<MidiSourceAssignment> midi_sources;
    /// SRTP 外部出力。送っていなければ std::nullopt。
    std::optional<ExternalStreamAssignment> external_stream;
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
/// 挿す先の挿入のうち 1 つでも plugins にない（読み込み中か失敗）ものがあれば、その挿す先を通る
/// 経路を切って無音にする。ストリップはソースから、Master とバスは後段（センド、リミッター、
/// 外部出力）への経路を切る。プラグインを通らない音を出さないため。
/// @param assignments       フェーダーとメーターの割り当て
/// @param master_entity_id  Master の entity_id（センドの送り元と送り先に使われる）
/// @param config            ルーティングの構成
/// @param feedback_approved 遅延ノードを通るループをオペレーターが承認した
/// @param plugins           読み込みの済んだ挿入
/// @return グラフと、外したセンド
/// スレッド安全性: 純粋関数。コントロールスレッドから呼ぶ。
[[nodiscard]] BuiltMixerGraph build_mixer_graph(const MixerAssignments& assignments,
                                                std::string_view master_entity_id,
                                                const control_protocol::RoutingConfig& config,
                                                bool feedback_approved,
                                                std::span<const session::InsertedPlugin> plugins);

}  // namespace vocroz::engine
