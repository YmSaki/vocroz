// SPDX-License-Identifier: AGPL-3.0-only
//
// make_vst3_plugin_processor()（factories.h）の実装。
// スキャン結果の記述からプラグインを探してインスタンスを作り、グラフのプラグインノードに渡せる
// チャンネル構成と、エンジン内部のサンプルレートとブロック長で処理の準備をする。

#include <array>
#include <memory>
#include <utility>

#include <juce_audio_processors_headless/juce_audio_processors_headless.h>

#include "vocroz/audio_core/constants.h"
#include "vocroz/plugin_host/factories.h"

#include "juce_plugin_description.h"
#include "juce_plugin_processor.h"

namespace vocroz::plugin_host {
namespace {

using common::make_unexpected;

// 試すメインバスのチャンネル数の組（入力、出力）。グラフのノードは入力 0〜2、出力 1〜2。
// エフェクトはステレオのバスに挿すことが多いので、ステレオ入出力から試す。
// モノラル入力でステレオ出力は、モノラルのチャンネルにステレオのリバーブを挿す構成。
struct ChannelPair {
    int inputs;
    int outputs;
};
constexpr std::array kEffectLayouts{ChannelPair{2, 2}, ChannelPair{1, 2}, ChannelPair{1, 1}};
constexpr std::array kInstrumentLayouts{ChannelPair{0, 2}, ChannelPair{0, 1}};

juce::AudioChannelSet channel_set(int channels) {
    switch (channels) {
        case 1:
            return juce::AudioChannelSet::mono();
        case 2:
            return juce::AudioChannelSet::stereo();
        default:
            return juce::AudioChannelSet::disabled();
    }
}

// メインバスを pair の構成にできれば true。メイン以外のバスは無効にしてある前提。
bool try_layout(juce::AudioPluginInstance& instance, ChannelPair pair) {
    juce::AudioProcessor::BusesLayout layout = instance.getBusesLayout();
    if (pair.inputs > 0 && layout.inputBuses.isEmpty()) {
        return false;
    }
    if (!layout.inputBuses.isEmpty()) {
        layout.inputBuses.getReference(0) = channel_set(pair.inputs);
    }
    if (layout.outputBuses.isEmpty()) {
        return false;
    }
    layout.outputBuses.getReference(0) = channel_set(pair.outputs);
    return instance.checkBusesLayoutSupported(layout) && instance.setBusesLayout(layout);
}

bool configure_layout(juce::AudioPluginInstance& instance, bool is_instrument) {
    instance.disableNonMainBuses();
    const auto try_all = [&instance](const auto& candidates) {
        for (const ChannelPair pair : candidates) {
            if (try_layout(instance, pair)) {
                return true;
            }
        }
        return false;
    };
    return is_instrument ? try_all(kInstrumentLayouts) : try_all(kEffectLayouts);
}

}  // namespace

common::Result<std::shared_ptr<audio_core::PluginProcessor>, PluginLoadError>
make_vst3_plugin_processor(const PluginDescription& description) {
    juce::VST3PluginFormatHeadless format;
    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile(found, juce::String::fromUTF8(description.file_path.c_str()));
    const juce::PluginDescription* match = nullptr;
    for (const juce::PluginDescription* candidate : found) {
        if (describes_same_plugin(*candidate, description)) {
            match = candidate;
            break;
        }
    }
    if (match == nullptr) {
        return make_unexpected(PluginLoadError::kNotFound);
    }

    constexpr auto kRate = static_cast<double>(audio_core::kSampleRate);
    constexpr auto kBlock = static_cast<int>(audio_core::kBlockSize);
    juce::String error;
    std::unique_ptr<juce::AudioPluginInstance> instance =
        format.createInstanceFromDescription(*match, kRate, kBlock, error);
    if (!instance) {
        return make_unexpected(PluginLoadError::kInstantiateFailed);
    }
    if (!configure_layout(*instance, match->isInstrument)) {
        return make_unexpected(PluginLoadError::kUnsupportedLayout);
    }
    instance->setRateAndBufferSizeDetails(kRate, kBlock);
    instance->setNonRealtime(false);
    instance->prepareToPlay(kRate, kBlock);
    return std::shared_ptr<audio_core::PluginProcessor>(
        std::make_shared<JucePluginProcessor>(std::move(instance)));
}

}  // namespace vocroz::plugin_host
