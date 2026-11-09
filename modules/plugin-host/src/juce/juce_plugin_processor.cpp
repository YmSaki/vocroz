// SPDX-License-Identifier: AGPL-3.0-only
//
// JucePluginProcessor の実装。

#include "juce_plugin_processor.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <span>
#include <utility>

#include "vocroz/audio_core/constants.h"
#include "vocroz/audio_core/midi_message.h"

#include "plugin_host_constants.h"

namespace vocroz::plugin_host {
namespace {

// MidiBuffer が 1 イベントの前に置く見出し（サンプル位置の int32 と、長さの uint16）のバイト数。
constexpr std::size_t kMidiEventHeaderBytes = sizeof(juce::int32) + sizeof(juce::uint16);

// MIDI バッファに事前に確保するバイト数。kMaxMidiEventsPerProcess 個の 3 バイトのメッセージを、
// 確保なしに積めるようにする。
constexpr int kMidiBufferBytes =
    static_cast<int>(kMaxMidiEventsPerProcess * (kMidiEventHeaderBytes + 3));

// 上限を超えたブロックで、止める側でないイベントに使える件数を返す。
// 止める側のイベント（Note Off、サステインのオフ、All Notes Off など）を落とすと音残りになるので、
// 上限の枠をまず止める側に割り当て、残りを発音側に回す。
std::size_t non_release_budget(std::span<const audio_core::MidiEvent> events) noexcept
    VOCROZ_NONBLOCKING {
    const auto releases = static_cast<std::size_t>(std::count_if(
        events.begin(), events.end(),
        [](const audio_core::MidiEvent& event) { return audio_core::is_release_message(event); }));
    return kMaxMidiEventsPerProcess - std::min(releases, kMaxMidiEventsPerProcess);
}

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
    fill_midi(context.midi);
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

void JucePluginProcessor::fill_midi(std::span<const audio_core::MidiEvent> events) noexcept
    VOCROZ_NONBLOCKING {
    midi_.clear();
    last_midi_offset_ = 0;
    if (events.size() <= kMaxMidiEventsPerProcess) {
        for (const audio_core::MidiEvent& event : events) {
            add_midi(event);
        }
        return;
    }
    // 上限を超えたら、確保が起きないように上限の件数までに絞る。並びは元のまま保ち、
    // 同じ位置の Note On と Note Off の順序を入れ替えない。
    std::size_t release_budget = kMaxMidiEventsPerProcess;
    std::size_t other_budget = non_release_budget(events);
    for (const audio_core::MidiEvent& event : events) {
        std::size_t& budget = audio_core::is_release_message(event) ? release_budget : other_budget;
        if (budget > 0) {
            --budget;
            add_midi(event);
        }
    }
}

void JucePluginProcessor::add_midi(const audio_core::MidiEvent& event) noexcept VOCROZ_NONBLOCKING {
    if (event.size == 0 || event.size > event.data.size()) {
        return;
    }
    const auto offset = static_cast<juce::int32>(event.sample_offset);
    if (offset < last_midi_offset_) {
        // 昇順の約束から外れたイベントは、JUCE の挿入で正しい位置に入れる。
        midi_.addEvent(event.data.data(), event.size, offset);
        return;
    }
    // MidiBuffer::addEvent() は挿入位置を先頭から線形に探すので、1 ブロックに積む件数の 2 乗の
    // 時間がかかる。Panic の 2,064 件で数ミリ秒になり、1 ブロック（128 サンプル、約 2.7 ms）に
    // 収まらない。イベントは sample_offset の昇順で届くので、末尾に MidiBuffer と同じ形式
    // （サンプル位置、長さ、本体）で直接書き足して、件数に比例する時間にする。
    // 形式は JUCE の内部の取り決めなので、JUCE を上げたら単体テストの読み戻しで確かめる
    // （docs/adr/0029-midi-and-vsti.md）。
    const int start = midi_.data.size();
    const auto bytes = static_cast<int>(kMidiEventHeaderBytes + event.size);
    midi_.data.insertMultiple(start, 0, bytes);
    juce::uint8* out = midi_.data.begin() + start;
    juce::writeUnaligned<juce::int32>(out, offset);
    juce::writeUnaligned<juce::uint16>(out + sizeof(juce::int32), event.size);
    std::memcpy(out + kMidiEventHeaderBytes, event.data.data(), event.size);
    last_midi_offset_ = offset;
}

}  // namespace vocroz::plugin_host
