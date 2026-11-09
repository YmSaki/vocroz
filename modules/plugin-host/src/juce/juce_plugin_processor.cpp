// SPDX-License-Identifier: AGPL-3.0-only
//
// JucePluginProcessor の実装。

#include "juce_plugin_processor.h"

#include <algorithm>
#include <utility>

#include "vocroz/audio_core/constants.h"

namespace vocroz::plugin_host {
namespace {

// MIDI バッファに事前に確保するバイト数。1 ブロックに 3 バイトのメッセージを 1,024 個まで、
// 確保なしに積めるようにする（MidiBuffer は 1 メッセージに時刻と長さの 6 バイトを足して持つ）。
constexpr int kMidiBufferBytes = 1024 * (3 + 6);

}  // namespace

JucePluginProcessor::JucePluginProcessor(std::unique_ptr<juce::AudioPluginInstance> instance)
    : instance_(std::move(instance)) {
    layout_.input_channels = static_cast<std::size_t>(instance_->getMainBusNumInputChannels());
    layout_.output_channels = static_cast<std::size_t>(instance_->getMainBusNumOutputChannels());
    midi_.ensureSize(kMidiBufferBytes);
}

JucePluginProcessor::~JucePluginProcessor() {
    instance_->releaseResources();
}

audio_core::PluginBusLayout JucePluginProcessor::layout() const noexcept {
    return layout_;
}

std::size_t JucePluginProcessor::latency_samples() const noexcept {
    return static_cast<std::size_t>(std::max(0, instance_->getLatencySamples()));
}

audio_core::PluginProcessStatus JucePluginProcessor::process(
    const audio_core::PluginProcessContext& context) noexcept VOCROZ_NONBLOCKING {
    if (failed_) {
        return audio_core::PluginProcessStatus::kFailed;
    }
    // グラフのバッファをそのまま参照する。チャンネルのポインターは AudioBuffer の内部の
    // 固定領域に収まるので、確保は起きない。
    juce::AudioBuffer<float> buffer(context.channels.data(),
                                    static_cast<int>(context.channels.size()),
                                    static_cast<int>(context.frames));
    midi_.clear();
    for (const audio_core::MidiEvent& event : context.midi) {
        midi_.addEvent(event.data.data(), event.size, static_cast<int>(event.sample_offset));
    }
    try {
        instance_->processBlock(buffer, midi_);
    } catch (...) {
        // プラグインの例外はモジュールの外へ出さない（AGENTS.md 6.2）。状態が壊れている
        // おそれがあるので、以降は呼ばずに失敗を返し続け、グラフ側でこのノードを無音にする。
        // アクセス違反などの構造化例外はここでは捕まえられない。クラッシュ隔離は
        // アウトオブプロセス実行（ADR-0009）で扱う。
        failed_ = true;
        return audio_core::PluginProcessStatus::kFailed;
    }
    return audio_core::PluginProcessStatus::kOk;
}

}  // namespace vocroz::plugin_host
