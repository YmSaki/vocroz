// SPDX-License-Identifier: AGPL-3.0-only
//
// Mixer の実装。フェーダー、メーター、レンダーグラフ、安全ミュートのゲートをまとめる。

#include "vocroz/audio_core/mixer.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <span>

#include "vocroz/audio_core/constants.h"
#include "vocroz/audio_core/gain.h"
#include "vocroz/audio_core/gain_ramp.h"
#include "vocroz/common/spsc_queue.h"

#include "audio_core_constants.h"
#include "fader_bank.h"
#include "graph_exchange.h"
#include "graph_validator.h"
#include "meter_bank.h"
#include "parameter_change.h"
#include "render_graph.h"

namespace vocroz::audio_core {
namespace {

using common::make_unexpected;

// ランプの長さを 5〜20ms に丸めてサンプル数にする。
std::size_t ramp_samples_of(float duration_ms) noexcept {
    // NaN は既定の 10ms とみなす。
    const float ms = std::isnan(duration_ms)
                         ? 10.0F
                         : std::clamp(duration_ms, kMinRampDurationMs, kMaxRampDurationMs);
    return static_cast<std::size_t>(ms * static_cast<float>(kSampleRate) / 1000.0F);
}

// デバイスへ書く値を -1〜1 の有限の値にする。グラフにリミッターがない構成でも、
// 不正な値や過大な値をデバイスへ渡さないための最後の防御。
float sanitize(float sample) noexcept {
    if (!std::isfinite(sample)) {
        return 0.0F;
    }
    return std::clamp(sample, -1.0F, 1.0F);
}

}  // namespace

class Mixer::Impl {
public:
    Impl(const MixerConfig& config, const common::Clock* midi_clock)
        : midi_clock_(midi_clock),
          faders_(config.fader_capacity, ramp_samples_of(config.ramp_duration_ms)),
          meters_(config.meter_capacity),
          safety_gate_(ramp_samples_of(config.ramp_duration_ms)) {
        // 安全ミュートのゲートは開いた状態で始める。初期ミュートはフェーダーが担う。
        safety_gate_.jump_to(1.0F);
        // 空の記述は必ず構築できる。最初から有効なグラフを置き、オーディオスレッドの分岐を減らす。
        auto empty = RenderGraph::build({}, 0, 0);
        exchange_.publish(std::move(empty).value());
    }

    common::Result<void, GraphError> apply_graph(const GraphDescription& description) {
        auto graph = RenderGraph::build(description, faders_.capacity(), meters_.capacity());
        if (!graph) {
            return make_unexpected(graph.error());
        }
        exchange_.publish(std::move(graph).value());
        return {};
    }

    [[nodiscard]] common::Result<void, GraphError> validate_graph(
        const GraphDescription& description) const {
        auto plan = plan_graph(description, faders_.capacity(), meters_.capacity());
        if (!plan) {
            return make_unexpected(plan.error());
        }
        return {};
    }

    common::Result<void, MixerError> push(const ParameterChange& change) {
        if (change.fader.value >= faders_.capacity()) {
            return make_unexpected(MixerError::kFaderOutOfRange);
        }
        if (!changes_.try_push(change)) {
            return make_unexpected(MixerError::kQueueFull);
        }
        return {};
    }

    void engage_safety_mute() noexcept {
        safety_muted_.store(true, std::memory_order_release);
    }

    void release_safety_mute() noexcept {
        safety_muted_.store(false, std::memory_order_release);
    }

    [[nodiscard]] bool is_safety_muted() const noexcept {
        return safety_muted_.load(std::memory_order_acquire);
    }

    [[nodiscard]] MeterReading read_meter(MeterId meter) noexcept {
        return meters_.read(meter);
    }

    void collect_garbage() noexcept {
        exchange_.collect();
    }

    [[nodiscard]] bool is_graph_settled() noexcept {
        return exchange_.is_settled();
    }

    void render(InterleavedBuffer out) noexcept VOCROZ_NONBLOCKING {
        apply_pending_changes();
        if (safety_muted_.load(std::memory_order_acquire)) {
            // 出力デバイスの消失などでは、クリックノイズより即座に止めることを優先する。
            safety_gate_.jump_to(0.0F);
        } else {
            safety_gate_.set_target(1.0F);
        }
        RenderGraph* graph = exchange_.acquire();

        // コールバックの先頭の時刻と、そこから出すサンプルの位置を対応づける。コールバックを
        // kBlockSize に分けたブロックは同じ時刻を基準にするので、分け方に関わらず MIDI
        // の位置が揃う。
        MidiBlockClock midi_clock;
        midi_clock.anchor_sample = rendered_samples_;
        if (midi_clock_ != nullptr) {
            midi_clock.has_anchor = true;
            midi_clock.anchor_time = midi_clock_->now();
        }

        const std::size_t frames = out.frames();
        for (std::size_t offset = 0; offset < frames; offset += kBlockSize) {
            const std::size_t block = std::min(kBlockSize, frames - offset);
            midi_clock.first_sample = rendered_samples_ + offset;
            midi_clock.frames = block;
            const std::span<const OutputBlock> outputs =
                graph != nullptr ? graph->process(block, faders_, meters_, midi_clock)
                                 : std::span<const OutputBlock>();
            write_block(out, offset, block, outputs);
        }
        rendered_samples_ += frames;
    }

private:
    void apply_pending_changes() noexcept VOCROZ_NONBLOCKING {
        // 取り出す数をキューの容量で打ち切り、生産者が積み続けてもコールバックが終わるようにする。
        for (std::size_t i = 0; i < kParameterQueueCapacity; ++i) {
            const auto change = changes_.try_pop();
            if (!change) {
                break;
            }
            faders_.apply(*change);
        }
    }

    // 出力ノードごとに、指定のチャンネルの組へ書く。どの出力ノードも書かないチャンネルは無音にし、
    // デバイスにないチャンネルへの出力は捨てる（別のチャンネルへ暗黙に振り替えない）。
    // モノラルのデバイスでは、チャンネル 0 から始まる出力ノードの L と R の平均を書く。
    void write_block(InterleavedBuffer out, std::size_t offset, std::size_t block,
                     std::span<const OutputBlock> outputs) noexcept VOCROZ_NONBLOCKING {
        const std::size_t channels = out.channels;
        float* frame = out.samples.data() + offset * channels;
        for (std::size_t i = 0; i < block; ++i, frame += channels) {
            const float gate = safety_gate_.next();
            std::fill(frame, frame + channels, 0.0F);
            for (const OutputBlock& output : outputs) {
                const float left = output.left[i] * gate;
                const float right = output.right[i] * gate;
                if (channels == 1) {
                    if (output.device_channel == 0) {
                        frame[0] = (left + right) * 0.5F;
                    }
                    continue;
                }
                if (output.device_channel < channels) {
                    frame[output.device_channel] = left;
                }
                if (output.device_channel + 1 < channels) {
                    frame[output.device_channel + 1] = right;
                }
            }
            for (std::size_t ch = 0; ch < channels; ++ch) {
                frame[ch] = sanitize(frame[ch]);
            }
        }
    }

    const common::Clock* midi_clock_;  // null なら MIDI のタイムスタンプを使わない
    FaderBank faders_;
    MeterBank meters_;
    GainRamp safety_gate_;  // オーディオスレッドだけが触る
    GraphExchange exchange_;
    common::SpscQueue<ParameterChange, kParameterQueueCapacity> changes_;
    std::atomic<bool> safety_muted_{false};
    std::uint64_t rendered_samples_ = 0;  // 出力した累計サンプル数。オーディオスレッドだけが触る
};

Mixer::Mixer(const MixerConfig& config) : impl_(std::make_unique<Impl>(config, nullptr)) {}

Mixer::Mixer(const MixerConfig& config, const common::Clock& midi_clock)
    : impl_(std::make_unique<Impl>(config, &midi_clock)) {}

Mixer::~Mixer() = default;

common::Result<void, GraphError> Mixer::apply_graph(const GraphDescription& description) {
    return impl_->apply_graph(description);
}

common::Result<void, GraphError> Mixer::validate_graph(const GraphDescription& description) const {
    return impl_->validate_graph(description);
}

common::Result<void, MixerError> Mixer::set_fader_gain_db(FaderId fader, float db) {
    ParameterChange change;
    change.kind = ParameterChange::Kind::kGain;
    change.fader = fader;
    change.gain = fader_db_to_linear(db);
    return impl_->push(change);
}

common::Result<void, MixerError> Mixer::set_fader_muted(FaderId fader, bool muted) {
    ParameterChange change;
    change.kind = ParameterChange::Kind::kMute;
    change.fader = fader;
    change.muted = muted;
    return impl_->push(change);
}

common::Result<void, MixerError> Mixer::reset_fader(FaderId fader) {
    ParameterChange change;
    change.kind = ParameterChange::Kind::kReset;
    change.fader = fader;
    return impl_->push(change);
}

void Mixer::engage_safety_mute() noexcept {
    impl_->engage_safety_mute();
}

void Mixer::release_safety_mute() noexcept {
    impl_->release_safety_mute();
}

bool Mixer::is_safety_muted() const noexcept {
    return impl_->is_safety_muted();
}

MeterReading Mixer::read_meter(MeterId meter) noexcept {
    return impl_->read_meter(meter);
}

void Mixer::collect_garbage() noexcept {
    impl_->collect_garbage();
}

bool Mixer::is_graph_settled() noexcept {
    return impl_->is_graph_settled();
}

void Mixer::render(InterleavedBuffer out) noexcept VOCROZ_NONBLOCKING {
    impl_->render(out);
}

}  // namespace vocroz::audio_core
