// SPDX-License-Identifier: AGPL-3.0-only
//
// オーディオスレッドで処理するレンダーグラフ。
// GraphDescription をオーディオスレッド外で検証し（graph_validator.h）、処理順（トポロジカル順）と
// 各ノードのバッファとディレイラインを事前に確保した形に変換する。構築後は構造を変えない
// （差し替えはスワップで行う）。
#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "vocroz/audio_core/graph_description.h"
#include "vocroz/audio_core/realtime.h"
#include "vocroz/common/result.h"

#include "audio_core_constants.h"
#include "delay_line.h"
#include "fader_bank.h"
#include "meter_bank.h"
#include "safety_limiter.h"

namespace vocroz::audio_core {

/// 出力ノード 1 つぶんの、1 ブロックの処理結果。
struct OutputBlock {
    std::size_t device_channel = 0;  ///< 書き込む出力デバイスのチャンネルの組の先頭
    const float* left = nullptr;  ///< L チャンネルのサンプル（処理したサンプル数ぶん）
    const float* right = nullptr;  ///< R チャンネルのサンプル（処理したサンプル数ぶん）
};

/// 検証済みで、処理に必要なメモリを確保済みのグラフ。
/// スレッド安全性: build() と破棄はオーディオスレッド外で行う。process()
/// はオーディオスレッドだけが呼ぶ。
class RenderGraph {
public:
    /// 記述を検証してレンダーグラフを構築する。
    /// @param description グラフの記述
    /// @param fader_capacity 使ってよい FaderId の上限（この値未満）
    /// @param meter_capacity 使ってよい MeterId の上限（この値未満）
    /// @return 構築したグラフ。記述が不正なら GraphError
    [[nodiscard]] static common::Result<std::unique_ptr<RenderGraph>, GraphError> build(
        const GraphDescription& description, std::size_t fader_capacity,
        std::size_t meter_capacity);

    RenderGraph(const RenderGraph&) = delete;
    RenderGraph& operator=(const RenderGraph&) = delete;
    RenderGraph(RenderGraph&&) = delete;
    RenderGraph& operator=(RenderGraph&&) = delete;
    ~RenderGraph() = default;

    /// 1 ブロックを処理する。
    /// @param frames 処理するサンプル数（kBlockSize 以下）
    /// @param faders フェーダーの状態
    /// @param meters メーターの状態
    /// @return 出力ノードごとの処理結果（出力ノードがなければ空）。次の process() まで有効
    [[nodiscard]] std::span<const OutputBlock> process(
        std::size_t frames, FaderBank& faders, MeterBank& meters) noexcept VOCROZ_NONBLOCKING;

private:
    struct Input {
        std::size_t position = 0;      // 送り元のノードの nodes_ 上の位置
        bool pre_fader = false;        // 送り元のフェーダーの前から取り出す
        std::optional<FaderId> level;  // センドレベル
        // プラグイン遅延補正。補正があるときだけ、送り元のチャンネルごとに 1 本ずつ確保する。
        std::vector<DelayLine> compensation;
    };

    struct Node {
        NodeKind kind = NodeKind::kOutput;
        std::shared_ptr<AudioSource> source;
        std::optional<FaderId> fader;
        std::optional<MeterId> meter;
        std::size_t channel_count = 0;        // 出力のチャンネル数（後段が読む数）
        std::size_t input_channel_count = 0;  // 入力の加算で受け取るチャンネル数
        std::size_t buffer_channel_count = 0;  // channels に確保した数（入力と出力の多い方）
        std::array<float*, kStereoChannels> channels{};
        // プリフェーダーのセンドの送り元のときだけ確保する、フェーダーの前の信号。
        std::array<float*, kStereoChannels> pre_fader{};
        // 送り元。遅延ノード以外は自分より前に処理するノードだけを指す。
        std::vector<Input> inputs;
        // kStrip の手動ディレイ（遅延があるときだけ 1 本）と、kDelay のチャンネルごとの遅延。
        std::vector<DelayLine> delays;
        SafetyLimiter limiter;                      // kLimiter のときだけ使う
        std::shared_ptr<PluginProcessor> plugin;    // kPlugin のときだけ使う
        std::shared_ptr<RecordingTrack> recording;  // kRecordTap のときだけ使う
        std::shared_ptr<PlaybackTrack> playback;    // kPlayer のときだけ使う
        // プラグインの処理が失敗したら立て、このグラフを破棄するまで出力を無音にする。
        // オーディオスレッドだけが読み書きする。
        bool plugin_failed = false;
    };

    RenderGraph() = default;

    void mix_inputs(Node& node, std::size_t frames, FaderBank& faders) noexcept VOCROZ_NONBLOCKING;
    void process_node(Node& node, std::size_t frames, FaderBank& faders,
                      MeterBank& meters) noexcept VOCROZ_NONBLOCKING;
    static void keep_pre_fader(Node& node, std::size_t frames) noexcept VOCROZ_NONBLOCKING;
    static void process_plugin(Node& node, std::size_t frames) noexcept VOCROZ_NONBLOCKING;

    std::vector<float>
        storage_;  // 全ノードのバッファ（channels、pre_fader、scratch_ はこの中を指す）
    std::vector<Node> nodes_;               // 処理順
    std::vector<std::size_t> delay_nodes_;  // 遅延ノードの nodes_ 上の位置
    std::vector<OutputBlock> outputs_;
    std::array<float*, kStereoChannels> scratch_{};  // センドレベルを掛けた信号の作業領域
};

}  // namespace vocroz::audio_core
