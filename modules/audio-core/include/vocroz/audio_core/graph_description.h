// SPDX-License-Identifier: AGPL-3.0-only
//
// 音声グラフの記述（値型）。
// コントロールスレッドでノードと結線を記述し、Mixer::apply_graph() に渡すと、
// オーディオスレッド外で検証してレンダーグラフを構築し、オーディオスレッドへスワップで渡す。
// Alpha の固定ミキサー（fixed_mixer_layout.h）も、Beta1 のルーティングも、この記述の 1
// 構成として作る。
#pragma once

#include <compare>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "vocroz/audio_core/ports/audio_source.h"

namespace vocroz::audio_core {

/// グラフ内のノードの識別子。1 つのグラフの中で一意にする。
struct NodeId {
    std::uint32_t value = 0;  ///< 識別子の値
    friend constexpr auto operator<=>(NodeId, NodeId) = default;
};

/// フェーダー（ゲインとミュート）の識別子。0 から MixerConfig::fader_capacity 未満。
/// フェーダーの状態はグラフの外（Mixer）が持つので、グラフを差し替えても値とランプが続く。
struct FaderId {
    std::uint16_t value = 0;  ///< 識別子の値
    friend constexpr auto operator<=>(FaderId, FaderId) = default;
};

/// メーターの識別子。0 から MixerConfig::meter_capacity 未満。
/// フェーダーと同じく、メーターの状態はグラフの外（Mixer）が持つ。
struct MeterId {
    std::uint16_t value = 0;  ///< 識別子の値
    friend constexpr auto operator<=>(MeterId, MeterId) = default;
};

/// ノードの種類。チャンネル数は種類で決まる。
enum class NodeKind {
    kSource,  ///< AudioSource からモノラルのサンプルを取り出す。入力を持たない
    kStrip,  ///< モノラルのチャンネルストリップ。プリフェーダーのメーター → フェーダー
    kBus,  ///< ステレオのバス。入力の加算 → フェーダー → ポストフェーダーのメーター
    kLimiter,  ///< ステレオの安全リミッター
    kOutput,  ///< 出力デバイスへの書き出し（ステレオ）。メーターは最終出力を測る
};

/// ノードの記述。各種類のファクトリー関数で作る。
struct NodeSpec {
    NodeId id;                            ///< ノードの識別子
    NodeKind kind = NodeKind::kOutput;    ///< ノードの種類
    std::shared_ptr<AudioSource> source;  ///< kSource のときのサンプルの取り出し先
    std::optional<FaderId> fader;         ///< kStrip、kBus のフェーダー
    std::optional<MeterId> meter;         ///< メーター（任意）

    /// ソースノードを作る。
    /// @param id ノードの識別子
    /// @param source サンプルの取り出し先（null にしない）
    [[nodiscard]] static NodeSpec make_source(NodeId id, std::shared_ptr<AudioSource> source);

    /// チャンネルストリップを作る。
    /// @param id ノードの識別子
    /// @param fader フェーダー
    /// @param meter プリフェーダーのメーター（付けないなら std::nullopt）
    [[nodiscard]] static NodeSpec make_strip(NodeId id, FaderId fader,
                                             std::optional<MeterId> meter);

    /// バスを作る。
    /// @param id ノードの識別子
    /// @param fader フェーダー
    /// @param meter ポストフェーダーのメーター（付けないなら std::nullopt）
    [[nodiscard]] static NodeSpec make_bus(NodeId id, FaderId fader, std::optional<MeterId> meter);

    /// 安全リミッターを作る。
    /// @param id ノードの識別子
    [[nodiscard]] static NodeSpec make_limiter(NodeId id);

    /// 出力ノードを作る。
    /// @param id ノードの識別子
    /// @param meter 最終出力のメーター（付けないなら std::nullopt）
    [[nodiscard]] static NodeSpec make_output(NodeId id, std::optional<MeterId> meter);
};

/// ノード間の結線。from の出力を to の入力に加算する。
struct Connection {
    NodeId from;  ///< 送り元
    NodeId to;    ///< 送り先
};

/// 音声グラフ全体の記述。ノードの並び順は処理順に影響しない（結線から決める）。
struct GraphDescription {
    std::vector<NodeSpec> nodes;          ///< ノード
    std::vector<Connection> connections;  ///< 結線
};

/// グラフの記述が不正な理由。
enum class GraphError {
    kDuplicateNodeId,  ///< 同じ NodeId のノードが複数ある
    kUnknownNode,      ///< 結線が存在しないノードを指している
    kInvalidConnection,  ///< ソースノードへの入力か、出力ノードからの出力がある
    kChannelMismatch,  ///< ステレオの出力をモノラルの入力（ストリップ）へつないでいる
    kCycle,            ///< 結線がループ（フィードバック経路）を作っている
    kMissingSource,    ///< ソースノードに AudioSource がない
    kMissingFader,     ///< ストリップかバスにフェーダーがない
    kFaderOutOfRange,  ///< フェーダーの識別子が容量を超えている
    kMeterOutOfRange,  ///< メーターの識別子が容量を超えている
    kDuplicateFader,   ///< 同じフェーダーを複数のノードが使っている
    kDuplicateMeter,   ///< 同じメーターを複数のノードが使っている
    kTooManyOutputs,   ///< 出力ノードが 2 つ以上ある
};

}  // namespace vocroz::audio_core
