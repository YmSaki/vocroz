// SPDX-License-Identifier: AGPL-3.0-only
//
// JUCE の AudioPluginInstance を、audio-core の PluginProcessor ポートとして呼ぶアダプター。
// グラフは audio-core が持つので、JUCE の AudioProcessorGraph は使わない。
// 理由は docs/adr/0009-beta-module-layout.md に書いた。
#pragma once

#include <cstddef>
#include <memory>

#include <juce_audio_processors_headless/juce_audio_processors_headless.h>

#include "vocroz/audio_core/ports/plugin_processor.h"

namespace vocroz::plugin_host {

/// 準備済みの AudioPluginInstance を、グラフの 1 ノードとして処理する。
/// スレッド安全性: 構築と破棄は JUCE のメッセージスレッドで行う。
/// layout() と latency_samples() はコントロールスレッド、process() はオーディオスレッドから呼ぶ。
class JucePluginProcessor final : public audio_core::PluginProcessor {
public:
    /// @param instance prepareToPlay() を済ませ、メインバスだけを有効にしたインスタンス
    explicit JucePluginProcessor(std::unique_ptr<juce::AudioPluginInstance> instance);
    JucePluginProcessor(const JucePluginProcessor&) = delete;
    JucePluginProcessor& operator=(const JucePluginProcessor&) = delete;
    JucePluginProcessor(JucePluginProcessor&&) = delete;
    JucePluginProcessor& operator=(JucePluginProcessor&&) = delete;
    ~JucePluginProcessor() override;

    [[nodiscard]] audio_core::PluginBusLayout layout() const noexcept override;
    [[nodiscard]] std::size_t latency_samples() const noexcept override;
    [[nodiscard]] audio_core::PluginProcessStatus process(
        const audio_core::PluginProcessContext& context) noexcept VOCROZ_NONBLOCKING override;

    /// 状態の保存と復元、エディターのために、包んでいるインスタンスを返す。
    /// @return インスタンス
    /// スレッド安全性: JUCE のメッセージスレッドから呼ぶ。
    [[nodiscard]] juce::AudioPluginInstance& instance() noexcept {
        return *instance_;
    }

private:
    std::unique_ptr<juce::AudioPluginInstance> instance_;
    audio_core::PluginBusLayout layout_;
    // processBlock() に渡す MIDI。構築時に容量を確保し、オーディオスレッドでは確保しない。
    juce::MidiBuffer midi_;
    // プラグインが例外を送出したら立て、以降は呼ばない。オーディオスレッドだけが読み書きする。
    bool failed_ = false;
};

}  // namespace vocroz::plugin_host
