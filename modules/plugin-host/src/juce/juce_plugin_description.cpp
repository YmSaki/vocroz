// SPDX-License-Identifier: AGPL-3.0-only
//
// juce_plugin_description.h の実装。

#include "juce_plugin_description.h"

#include <algorithm>

namespace vocroz::plugin_host {
namespace {

std::size_t channel_count(int channels) {
    return static_cast<std::size_t>(std::max(0, channels));
}

}  // namespace

PluginDescription to_plugin_description(const juce::PluginDescription& description) {
    PluginDescription result;
    result.name = description.name.toStdString();
    result.vendor = description.manufacturerName.toStdString();
    result.version = description.version.toStdString();
    result.file_path = description.fileOrIdentifier.toStdString();
    result.identifier = description.createIdentifierString().toStdString();
    result.is_instrument = description.isInstrument;
    result.input_channels = channel_count(description.numInputChannels);
    result.output_channels = channel_count(description.numOutputChannels);
    return result;
}

bool describes_same_plugin(const juce::PluginDescription& candidate,
                           const PluginDescription& description) {
    return candidate.createIdentifierString().toStdString() == description.identifier;
}

}  // namespace vocroz::plugin_host
