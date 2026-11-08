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

NodeSpec NodeSpec::make_strip(NodeId id, FaderId fader, std::optional<MeterId> meter) {
    NodeSpec spec;
    spec.id = id;
    spec.kind = NodeKind::kStrip;
    spec.fader = fader;
    spec.meter = meter;
    return spec;
}

NodeSpec NodeSpec::make_bus(NodeId id, FaderId fader, std::optional<MeterId> meter) {
    NodeSpec spec = make_strip(id, fader, meter);
    spec.kind = NodeKind::kBus;
    return spec;
}

NodeSpec NodeSpec::make_limiter(NodeId id) {
    NodeSpec spec;
    spec.id = id;
    spec.kind = NodeKind::kLimiter;
    return spec;
}

NodeSpec NodeSpec::make_output(NodeId id, std::optional<MeterId> meter) {
    NodeSpec spec;
    spec.id = id;
    spec.kind = NodeKind::kOutput;
    spec.meter = meter;
    return spec;
}

}  // namespace vocroz::audio_core
