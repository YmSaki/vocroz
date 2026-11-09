// SPDX-License-Identifier: AGPL-3.0-only
//
// MidiRouter の実装。

#include "midi_router.h"

#include <algorithm>
#include <limits>

#include "audio_core_constants.h"

namespace vocroz::audio_core {

void MidiRouter::build(const GraphDescription& description, const GraphPlan& plan,
                       const std::vector<std::size_t>& position_of) {
    sources_.resize(description.midi_sources.size());
    for (std::size_t i = 0; i < sources_.size(); ++i) {
        sources_[i].source = description.midi_sources[i].source;
        sources_[i].events.resize(kMaxMidiEventsPerBlock);
    }
    destination_of_.assign(description.nodes.size(), std::nullopt);
    for (std::size_t spec_index = 0; spec_index < description.nodes.size(); ++spec_index) {
        const NodeSpec& spec = description.nodes[spec_index];
        if (spec.kind != NodeKind::kPlugin) {
            continue;
        }
        Destination destination;
        destination.plugin = spec.plugin.get();
        for (const std::size_t source : plan.midi_sources_of[spec_index]) {
            destination.routes.push_back(routes_.size());
            routes_.push_back(Route{source, {}});
        }
        destination.cursors.resize(destination.routes.size());
        // 外れた経路と Panic の Note Off（経路ごとと引き継いだぶん）と、各ソースのイベントが
        // すべて入る大きさにし、どの組み合わせでも落とさない。
        const std::size_t routes = destination.routes.size();
        destination.merged.resize(kMaxReleaseEvents * (routes + 1) +
                                  kMaxMidiEventsPerBlock * routes);
        destination_of_[position_of[spec_index]] = destinations_.size();
        destinations_.push_back(std::move(destination));
    }
}

void MidiRouter::pull_sources(const MidiBlockClock& clock,
                              std::size_t frames) noexcept VOCROZ_NONBLOCKING {
    MidiBlockClock block = clock;
    block.frames = frames;
    for (Source& source : sources_) {
        const MidiPullResult result = source.source->pull(block, source.events);
        source.count = std::min(result.count, source.events.size());
        source.panic = result.panic;
        // ソースの実装の誤りで範囲外や逆順の位置が来ても、プラグインには契約どおり
        // （ブロック内、昇順）に渡す。
        std::uint32_t last = 0;
        const auto last_frame = static_cast<std::uint32_t>(frames > 0 ? frames - 1 : 0);
        for (std::size_t i = 0; i < source.count; ++i) {
            MidiEvent& event = source.events[i];
            last = std::max(std::min(event.sample_offset, last_frame), last);
            event.sample_offset = last;
        }
    }
}

std::span<const MidiEvent> MidiRouter::events_for(std::size_t node_position) noexcept
    VOCROZ_NONBLOCKING {
    if (node_position >= destination_of_.size() || !destination_of_[node_position]) {
        return {};
    }
    Destination& destination = destinations_[*destination_of_[node_position]];
    const std::span<MidiEvent> out(destination.merged);
    std::size_t count = destination.orphaned.release_all(out, 0);
    for (const std::size_t r : destination.routes) {
        Route& route = routes_[r];
        if (sources_[route.source].panic) {
            count += route.notes.release_all(out.subspan(count), 0);
        }
    }
    // 経路ごとに昇順のイベントを、サンプル位置の順に合わせる（k 個の列のマージ）。
    // 経路の数は少ないので、毎回すべての経路の先頭を比べる。
    std::fill(destination.cursors.begin(), destination.cursors.end(), 0);
    while (count < out.size()) {
        std::size_t best = destination.routes.size();
        std::uint32_t best_offset = std::numeric_limits<std::uint32_t>::max();
        for (std::size_t i = 0; i < destination.routes.size(); ++i) {
            const Source& source = sources_[routes_[destination.routes[i]].source];
            const std::size_t cursor = destination.cursors[i];
            // 同じ位置なら先に記述した経路を選ぶ（< で比べる）。
            if (cursor < source.count && source.events[cursor].sample_offset < best_offset) {
                best = i;
                best_offset = source.events[cursor].sample_offset;
            }
        }
        if (best == destination.routes.size()) {
            break;
        }
        Route& route = routes_[destination.routes[best]];
        const MidiEvent& event = sources_[route.source].events[destination.cursors[best]++];
        route.notes.observe(event);
        out[count++] = event;
    }
    return out.first(count);
}

void MidiRouter::adopt(const MidiRouter& previous) noexcept VOCROZ_NONBLOCKING {
    for (const Destination& old_destination : previous.destinations_) {
        // 新しいグラフで同じプラグインを探す。プラグインの数は少ないので線形に探す。
        Destination* destination = nullptr;
        for (Destination& candidate : destinations_) {
            if (candidate.plugin == old_destination.plugin) {
                destination = &candidate;
                break;
            }
        }
        if (destination == nullptr) {
            continue;
        }
        destination->orphaned.merge(old_destination.orphaned);
        for (const std::size_t old_r : old_destination.routes) {
            const Route& old_route = previous.routes_[old_r];
            if (!old_route.notes.any()) {
                continue;
            }
            const MidiSource* old_source = previous.sources_[old_route.source].source.get();
            Route* kept = nullptr;
            for (const std::size_t r : destination->routes) {
                if (sources_[routes_[r].source].source.get() == old_source) {
                    kept = &routes_[r];
                    break;
                }
            }
            if (kept != nullptr) {
                kept->notes = old_route.notes;
            } else {
                destination->orphaned.merge(old_route.notes);
            }
        }
    }
}

}  // namespace vocroz::audio_core
