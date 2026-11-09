// SPDX-License-Identifier: AGPL-3.0-only
//
// JucePluginInstance（juce_plugin_instance.h）の実装。

#include "juce_plugin_instance.h"

#include <limits>
#include <utility>

#include "juce_plugin_processor.h"

namespace vocroz::plugin_host {

using common::make_unexpected;
using session::PluginStateError;

JucePluginInstance::JucePluginInstance(std::shared_ptr<JucePluginProcessor> processor)
    : processor_(std::move(processor)) {}

std::shared_ptr<audio_core::PluginProcessor> JucePluginInstance::processor() const {
    return processor_;
}

common::Result<std::vector<std::uint8_t>, PluginStateError> JucePluginInstance::save_state() {
    // 他社のプラグインのコードを呼ぶので、例外を送出されてもエンジンを落とさない。
    try {
        juce::MemoryBlock block;
        processor_->instance().getStateInformation(block);
        const auto* begin = static_cast<const std::uint8_t*>(block.getData());
        return std::vector<std::uint8_t>(begin, begin + block.getSize());
    } catch (...) {
        return make_unexpected(PluginStateError::kRejected);
    }
}

common::Result<void, PluginStateError> JucePluginInstance::restore_state(
    std::span<const std::uint8_t> state) {
    // JUCE の API はサイズを int で受け取るので、それを超える状態は渡せない。
    if (state.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return make_unexpected(PluginStateError::kRejected);
    }
    try {
        processor_->instance().setStateInformation(state.data(), static_cast<int>(state.size()));
        return {};
    } catch (...) {
        return make_unexpected(PluginStateError::kRejected);
    }
}

}  // namespace vocroz::plugin_host
