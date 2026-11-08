// SPDX-License-Identifier: AGPL-3.0-only
//
// オーディオスレッドで処理するレンダーグラフ。
// GraphDescription をオーディオスレッド外で検証し、処理順（トポロジカル順）と
// 各ノードのバッファを事前に確保した形に変換する。構築後は構造を変えない（差し替えはスワップで行う）。
#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <vector>

#include "vocroz/audio_core/graph_description.h"
#include "vocroz/audio_core/realtime.h"
#include "vocroz/common/result.h"

#include "audio_core_constants.h"
#include "fader_bank.h"
#include "meter_bank.h"
#include "safety_limiter.h"

namespace vocroz::audio_core {

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
    /// @return 出力ノードのステレオのチャンネル（L、R）。出力ノードがなければ nullptr
    [[nodiscard]] const float* const* process(std::size_t frames, FaderBank& faders,
                                              MeterBank& meters) noexcept VOCROZ_NONBLOCKING;

private:
    struct Node {
        NodeKind kind = NodeKind::kOutput;
        std::shared_ptr<AudioSource> source;
        std::optional<FaderId> fader;
        std::optional<MeterId> meter;
        std::size_t channel_count = 0;
        std::array<float*, kStereoChannels> channels{};
        std::vector<std::size_t> inputs;  // 入力ノードの nodes_ 上の位置（自分より前）
        SafetyLimiter limiter;            // kLimiter のときだけ使う
    };

    RenderGraph() = default;

    void mix_inputs(Node& node, std::size_t frames) noexcept VOCROZ_NONBLOCKING;

    std::vector<float> storage_;  // 全ノードのバッファ（channels はこの中を指す）
    std::vector<Node> nodes_;     // トポロジカル順
    const float* const* output_ = nullptr;
};

}  // namespace vocroz::audio_core
