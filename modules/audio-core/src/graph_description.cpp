// SPDX-License-Identifier: AGPL-3.0-only
//
// NodeSpec のファクトリー関数の実装。

#include "vocroz/audio_core/graph_description.h"

#include <utility>

namespace vocroz::audio_core {

NodeSpec NodeSpec::make_source(NodeId id, std::shared_ptr<AudioSource> source) {
    NodeSpec spec;
    spec.id = id;
    spec.kind = NodeKind::kSource;
    spec.source = std::move(source);
    return spec;
}

NodeSpec NodeSpec::make_strip(NodeId id, FaderId fader, std::optional<MeterId> meter,
                              std::size_t delay_samples, std::size_t channels) {
    NodeSpec spec;
    spec.id = id;
    spec.kind = NodeKind::kStrip;
    spec.fader = fader;
    spec.meter = meter;
    spec.delay_samples = delay_samples;
    spec.channels = channels;
    return spec;
}

NodeSpec NodeSpec::make_bus(NodeId id, FaderId fader, std::optional<MeterId> meter) {
    NodeSpec spec = make_strip(id, fader, meter);
    spec.kind = NodeKind::kBus;
    return spec;
}

NodeSpec NodeSpec::make_delay(NodeId id, std::size_t delay_samples) {
    NodeSpec spec;
    spec.id = id;
    spec.kind = NodeKind::kDelay;
    spec.delay_samples = delay_samples;
    return spec;
}

NodeSpec NodeSpec::make_limiter(NodeId id) {
    NodeSpec spec;
    spec.id = id;
    spec.kind = NodeKind::kLimiter;
    return spec;
}

NodeSpec NodeSpec::make_output(NodeId id, std::optional<MeterId> meter,
                               std::size_t device_channel) {
    NodeSpec spec;
    spec.id = id;
    spec.kind = NodeKind::kOutput;
    spec.meter = meter;
    spec.device_channel = device_channel;
    return spec;
}

NodeSpec NodeSpec::make_plugin(NodeId id, std::shared_ptr<PluginProcessor> plugin) {
    NodeSpec spec;
    spec.id = id;
    spec.kind = NodeKind::kPlugin;
    if (plugin) {
        spec.plugin_layout = plugin->layout();
        spec.latency_samples = plugin->latency_samples();
    }
    spec.plugin = std::move(plugin);
    return spec;
}

NodeSpec NodeSpec::make_record_tap(NodeId id, std::shared_ptr<RecordingTrack> track) {
    NodeSpec spec;
    spec.id = id;
    spec.kind = NodeKind::kRecordTap;
    spec.recording = std::move(track);
    return spec;
}

NodeSpec NodeSpec::make_player(NodeId id, std::shared_ptr<PlaybackTrack> track) {
    NodeSpec spec;
    spec.id = id;
    spec.kind = NodeKind::kPlayer;
    spec.playback = std::move(track);
    return spec;
}

NodeSpec NodeSpec::make_return_tap(NodeId id, std::shared_ptr<AudioSink> sink) {
    NodeSpec spec;
    spec.id = id;
    spec.kind = NodeKind::kReturnTap;
    spec.sink = std::move(sink);
    return spec;
}

}  // namespace vocroz::audio_core
