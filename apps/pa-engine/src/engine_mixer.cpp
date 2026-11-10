// SPDX-License-Identifier: AGPL-3.0-only
//
// engine_mixer.h の実装。

#include "engine_mixer.h"

#include <algorithm>
#include <limits>
#include <map>
#include <utility>

#include "vocroz/audio_core/gain.h"
#include "vocroz/session/entity_ids.h"

#include "engine_constants.h"

namespace vocroz::engine {
namespace {

namespace ac = audio_core;
namespace cp = control_protocol;

[[nodiscard]] float to_mixer_db(cp::GainDb gain_db) noexcept {
    return gain_db ? static_cast<float>(*gain_db) : -std::numeric_limits<float>::infinity();
}

[[nodiscard]] std::optional<double> peak_db_of(const ac::MeterReading& reading) noexcept {
    if (!(reading.peak > 0.0F)) {
        return std::nullopt;
    }
    return static_cast<double>(ac::linear_to_db(reading.peak));
}

}  // namespace

std::optional<std::uint16_t> EngineMixer::IdPool::acquire() {
    const auto it = std::find(used_.begin(), used_.end(), false);
    if (it == used_.end()) {
        return std::nullopt;
    }
    *it = true;
    return static_cast<std::uint16_t>(it - used_.begin());
}

void EngineMixer::IdPool::release(std::uint16_t id) {
    if (id < used_.size()) {
        used_[id] = false;
    }
}

EngineMixer::EngineMixer(ac::Mixer& mixer, common::Logger& logger)
    : mixer_(mixer), logger_(logger), faders_(kMixerFaderCapacity), meters_(kMixerMeterCapacity) {
    // Master は起動時に 1 回だけ割り当て、以降変えない。空のプールからは必ず取れる。
    assignments_.master_fader = ac::FaderId{faders_.acquire().value_or(0)};
    assignments_.master_meter = ac::MeterId{meters_.acquire().value_or(0)};
    // 何もない状態でも Master → リミッター → メインの出力の経路を置いておく。
    // Master はミュートで始まるので、ここで音は出ない。
    (void)rebuild(assignments_, routing_, feedback_approved_, plugins_);
}

void EngineMixer::add_channel(const std::string& strip_id) {
    if (find_strip(strip_id) != nullptr) {
        return;
    }
    const auto fader = faders_.acquire();
    const auto meter = meters_.acquire();
    if (!fader || !meter) {
        // session のストリップの上限（64
        // 本）は容量に収まるので、ここに来るのは容量の見積もりの誤り。
        // ストリップを置かないので、その参加者の音は出ない（フェイルセーフ）。
        if (fader) {
            faders_.release(*fader);
        }
        if (meter) {
            meters_.release(*meter);
        }
        logger_.log(common::LogLevel::kError, "ミキサーのフェーダーが足りずストリップを置けません");
        return;
    }
    // グラフに入れる前に初期状態（ミュート、0 dB）に戻す（新規参加時の初期ミュート）。
    submit(FaderOp{FaderOp::Kind::kReset, ac::FaderId{*fader}});
    assignments_.strips.push_back(
        StripAssignment{strip_id, ac::FaderId{*fader}, ac::MeterId{*meter}, nullptr});
    if (!rebuild(assignments_, routing_, feedback_approved_, plugins_)) {
        logger_.log(common::LogLevel::kError, "ストリップを追加したグラフを適用できませんでした");
    }
}

void EngineMixer::set_strip_removal_handler(std::function<void(const std::string&)> handler) {
    on_strip_removal_ = std::move(handler);
}

void EngineMixer::remove_channel(const std::string& strip_id) {
    const auto it = std::find_if(
        assignments_.strips.begin(), assignments_.strips.end(),
        [&strip_id](const StripAssignment& strip) { return strip.strip_id == strip_id; });
    if (it == assignments_.strips.end()) {
        return;
    }
    // 子機の MIDI の Panic を先に求める。グラフから外す前に Note Off を送れるようにするため。
    if (on_strip_removal_) {
        on_strip_removal_(strip_id);
    }
    const auto fader = it->fader;
    const auto meter = it->meter;
    assignments_.strips.erase(it);
    std::erase_if(assignments_.midi_sources,
                  [key = strip_midi_key(strip_id)](const auto& midi) { return midi.key == key; });
    if (!rebuild(assignments_, routing_, feedback_approved_, plugins_)) {
        logger_.log(common::LogLevel::kError, "ストリップを外したグラフを適用できませんでした");
    }
    // 返す前にミュートに戻しておき、次に割り当てたストリップが前の値で鳴らないようにする。
    submit(FaderOp{FaderOp::Kind::kReset, fader});
    faders_.release(fader.value);
    meters_.release(meter.value);
}

void EngineMixer::set_channel_muted(const std::string& strip_id, bool muted) {
    if (const auto* strip = find_strip(strip_id)) {
        submit(FaderOp{FaderOp::Kind::kMute, strip->fader, muted});
    }
}

void EngineMixer::set_channel_gain(const std::string& strip_id, cp::GainDb gain_db) {
    if (const auto* strip = find_strip(strip_id)) {
        submit(FaderOp{FaderOp::Kind::kGain, strip->fader, true, to_mixer_db(gain_db)});
    }
}

void EngineMixer::set_master_muted(bool muted) {
    if (muted) {
        submit(FaderOp{FaderOp::Kind::kMute, assignments_.master_fader, true});
        return;
    }
    // ミュートの解除はオペレーターの操作だけで起きる（session が判断する）。
    // 出力デバイスの抜去でかけた安全ミュートも、この操作で一緒に解く。
    mixer_.release_safety_mute();
    submit(FaderOp{FaderOp::Kind::kMute, assignments_.master_fader, false});
}

void EngineMixer::set_master_gain(cp::GainDb gain_db) {
    submit(FaderOp{FaderOp::Kind::kGain, assignments_.master_fader, true, to_mixer_db(gain_db)});
}

bool EngineMixer::apply_routing(const cp::RoutingConfig& config, bool feedback_approved,
                                std::span<const session::InsertedPlugin> plugins) {
    MixerAssignments candidate = assignments_;
    IdPool faders = faders_;
    IdPool meters = meters_;
    std::vector<ac::FaderId> added;
    std::vector<ac::FaderId> released;
    if (!assign_routes(config, candidate, faders, meters, added, released)) {
        logger_.log(common::LogLevel::kError, "ルーティングのフェーダーが足りず適用できません");
        return false;
    }
    // 新しいバスとセンドは、グラフに入れる前にミュートで 0 dB にする（初期ミュート）。
    for (const auto fader : added) {
        submit(FaderOp{FaderOp::Kind::kReset, fader});
    }
    if (!rebuild(candidate, config, feedback_approved, plugins)) {
        logger_.log(common::LogLevel::kWarning, "ルーティングのグラフをミキサーが受け付けません");
        return false;
    }
    for (const auto fader : released) {
        submit(FaderOp{FaderOp::Kind::kReset, fader});
    }
    assignments_ = std::move(candidate);
    faders_ = std::move(faders);
    meters_ = std::move(meters);
    routing_ = config;
    feedback_approved_ = feedback_approved;
    plugins_.assign(plugins.begin(), plugins.end());
    return true;
}

void EngineMixer::set_route_muted(const std::string& entity_id, bool muted) {
    const auto it = assignments_.route_faders.find(entity_id);
    if (it != assignments_.route_faders.end()) {
        submit(FaderOp{FaderOp::Kind::kMute, it->second, muted});
    }
}

void EngineMixer::set_route_gain(const std::string& entity_id, cp::GainDb gain_db) {
    const auto it = assignments_.route_faders.find(entity_id);
    if (it != assignments_.route_faders.end()) {
        submit(FaderOp{FaderOp::Kind::kGain, it->second, true, to_mixer_db(gain_db)});
    }
}

bool EngineMixer::attach_source(const std::string& strip_id,
                                std::shared_ptr<ac::AudioSource> source) {
    auto* strip = find_strip(strip_id);
    if (strip == nullptr || !source) {
        return false;
    }
    auto previous = std::exchange(strip->source, std::move(source));
    if (!rebuild(assignments_, routing_, feedback_approved_, plugins_)) {
        strip->source = std::move(previous);
        logger_.log(common::LogLevel::kError,
                    "受信ストリームをつないだグラフを適用できませんでした");
        return false;
    }
    return true;
}

void EngineMixer::detach_source(const std::string& strip_id) {
    auto* strip = find_strip(strip_id);
    if (strip == nullptr || !strip->source) {
        return;
    }
    strip->source.reset();
    if (!rebuild(assignments_, routing_, feedback_approved_, plugins_)) {
        logger_.log(common::LogLevel::kError, "受信ストリームを外したグラフを適用できませんでした");
    }
}

bool EngineMixer::attach_return(std::uint64_t key, std::vector<std::string> own_strip_ids,
                                std::shared_ptr<ac::AudioSink> sink) {
    if (!sink || own_strip_ids.empty() ||
        !std::ranges::all_of(own_strip_ids,
                             [&](const std::string& id) { return find_strip(id) != nullptr; })) {
        return false;
    }
    auto& returns = assignments_.returns;
    const auto previous = returns;
    const auto it = std::ranges::find(returns, key, &ReturnAssignment::key);
    ReturnAssignment assignment{key, std::move(own_strip_ids), std::move(sink)};
    if (it == returns.end()) {
        returns.push_back(std::move(assignment));
    } else {
        *it = std::move(assignment);
    }
    if (!rebuild(assignments_, routing_, feedback_approved_, plugins_)) {
        returns = previous;
        logger_.log(common::LogLevel::kError, "返送をつないだグラフを適用できませんでした");
        return false;
    }
    return true;
}

bool EngineMixer::attach_external_stream(std::string source_id,
                                         std::shared_ptr<ac::AudioStreamSink> sink) {
    if (!sink || source_id.empty()) {
        return false;
    }
    const auto previous = assignments_.external_stream;
    assignments_.external_stream = ExternalStreamAssignment{std::move(source_id), std::move(sink)};
    if (!rebuild(assignments_, routing_, feedback_approved_, plugins_)) {
        assignments_.external_stream = previous;
        logger_.log(common::LogLevel::kError, "外部出力をつないだグラフを適用できませんでした");
        return false;
    }
    return true;
}

void EngineMixer::detach_external_stream() {
    if (!assignments_.external_stream) {
        return;
    }
    assignments_.external_stream.reset();
    if (!rebuild(assignments_, routing_, feedback_approved_, plugins_)) {
        logger_.log(common::LogLevel::kError, "外部出力を外したグラフを適用できませんでした");
    }
}

void EngineMixer::detach_return(std::uint64_t key) {
    auto& returns = assignments_.returns;
    const auto it = std::ranges::find(returns, key, &ReturnAssignment::key);
    if (it == returns.end()) {
        return;
    }
    returns.erase(it);
    if (!rebuild(assignments_, routing_, feedback_approved_, plugins_)) {
        logger_.log(common::LogLevel::kError, "返送を外したグラフを適用できませんでした");
    }
}

bool EngineMixer::attach_midi_source(const std::string& key,
                                     std::shared_ptr<ac::MidiSource> source) {
    if (!source) {
        return false;
    }
    auto& sources = assignments_.midi_sources;
    const auto previous = sources;
    const auto it = std::ranges::find(sources, key, &MidiSourceAssignment::key);
    if (it == sources.end()) {
        sources.push_back(
            MidiSourceAssignment{key, ac::MidiSourceId{next_midi_source_++}, std::move(source)});
    } else {
        it->source = std::move(source);
    }
    if (!rebuild(assignments_, routing_, feedback_approved_, plugins_)) {
        sources = previous;
        logger_.log(common::LogLevel::kError, "MIDI ソースをつないだグラフを適用できませんでした");
        return false;
    }
    return true;
}

void EngineMixer::detach_midi_source(const std::string& key) {
    auto& sources = assignments_.midi_sources;
    const auto it = std::ranges::find(sources, key, &MidiSourceAssignment::key);
    if (it == sources.end()) {
        return;
    }
    sources.erase(it);
    if (!rebuild(assignments_, routing_, feedback_approved_, plugins_)) {
        logger_.log(common::LogLevel::kError, "MIDI ソースを外したグラフを適用できませんでした");
    }
}

void EngineMixer::collect() {
    while (!retry_.empty() && try_submit(retry_.front())) {
        retry_.pop_front();
    }
    mixer_.collect_garbage();
}

std::vector<cp::MeterLevel> EngineMixer::read_meters() {
    std::vector<cp::MeterLevel> levels;
    levels.reserve(assignments_.strips.size() + 1);
    for (const auto& strip : assignments_.strips) {
        levels.push_back(
            cp::MeterLevel{strip.strip_id, peak_db_of(mixer_.read_meter(strip.meter))});
    }
    levels.push_back(cp::MeterLevel{session::kMasterEntityId,
                                    peak_db_of(mixer_.read_meter(assignments_.master_meter))});
    std::sort(levels.begin(), levels.end(), [](const cp::MeterLevel& a, const cp::MeterLevel& b) {
        return a.entity_id < b.entity_id;
    });
    return levels;
}

StripAssignment* EngineMixer::find_strip(std::string_view strip_id) {
    const auto it = std::find_if(
        assignments_.strips.begin(), assignments_.strips.end(),
        [strip_id](const StripAssignment& strip) { return strip.strip_id == strip_id; });
    return it == assignments_.strips.end() ? nullptr : &*it;
}

bool EngineMixer::rebuild(const MixerAssignments& assignments, const cp::RoutingConfig& config,
                          bool feedback_approved,
                          std::span<const session::InsertedPlugin> plugins) {
    auto built = build_mixer_graph(assignments, session::kMasterEntityId, config, feedback_approved,
                                   plugins);
    if (const auto applied = mixer_.apply_graph(built.graph); !applied) {
        return false;
    }
    mute_dropped_sends(built.dropped_sends);
    return true;
}

void EngineMixer::submit(const FaderOp& op) {
    // 先に積んだ操作が残っていれば、追い越さないよう後ろに並べる。
    if (!retry_.empty() || !try_submit(op)) {
        retry_.push_back(op);
        // Master のミュートが遅れると音が出続けるので、送り直しを待たずに安全ミュートで止める。
        // 解除はオペレーターのミュート解除の操作（set_master_muted(false)）で行う。
        if (op.kind == FaderOp::Kind::kMute && op.muted && op.fader == assignments_.master_fader) {
            mixer_.engage_safety_mute();
        }
    }
}

bool EngineMixer::try_submit(const FaderOp& op) {
    switch (op.kind) {
        case FaderOp::Kind::kReset:
            return mixer_.reset_fader(op.fader).has_value();
        case FaderOp::Kind::kMute:
            return mixer_.set_fader_muted(op.fader, op.muted).has_value();
        case FaderOp::Kind::kGain:
            return mixer_.set_fader_gain_db(op.fader, op.gain_db).has_value();
    }
    return false;
}

void EngineMixer::mute_dropped_sends(const std::vector<std::string>& dropped) {
    for (const auto& send_id : dropped) {
        const auto it = assignments_.route_faders.find(send_id);
        if (it != assignments_.route_faders.end()) {
            submit(FaderOp{FaderOp::Kind::kMute, it->second, true});
        }
    }
    if (!dropped.empty()) {
        logger_.log(common::LogLevel::kWarning,
                    "参照先のないセンドをグラフから外し、ミュートしました");
    }
}

bool EngineMixer::assign_routes(const cp::RoutingConfig& config, MixerAssignments& assignments,
                                IdPool& faders, IdPool& meters, std::vector<ac::FaderId>& added,
                                std::vector<ac::FaderId>& released) const {
    // 同じ entity_id のバスとセンドは、フェーダーとミュートの値を引き継ぐ（ADR-0026）。
    std::map<std::string, ac::FaderId, std::less<>> next_faders;
    std::map<std::string, ac::MeterId, std::less<>> next_meters;
    const auto keep_or_acquire = [&](const std::string& id) -> bool {
        if (const auto it = assignments.route_faders.find(id);
            it != assignments.route_faders.end()) {
            next_faders.emplace(id, it->second);
            return true;
        }
        const auto fader = faders.acquire();
        if (!fader) {
            return false;
        }
        next_faders.emplace(id, ac::FaderId{*fader});
        added.push_back(ac::FaderId{*fader});
        return true;
    };
    for (const auto& bus : config.buses) {
        if (!keep_or_acquire(bus.entity_id)) {
            return false;
        }
        if (const auto it = assignments.bus_meters.find(bus.entity_id);
            it != assignments.bus_meters.end()) {
            next_meters.emplace(bus.entity_id, it->second);
        } else if (const auto meter = meters.acquire()) {
            next_meters.emplace(bus.entity_id, ac::MeterId{*meter});
        } else {
            return false;
        }
    }
    for (const auto& send : config.sends) {
        if (!keep_or_acquire(send.entity_id)) {
            return false;
        }
    }
    for (const auto& [id, fader] : assignments.route_faders) {
        if (!next_faders.contains(id)) {
            faders.release(fader.value);
            released.push_back(fader);
        }
    }
    for (const auto& [id, meter] : assignments.bus_meters) {
        if (!next_meters.contains(id)) {
            meters.release(meter.value);
        }
    }
    assignments.route_faders = std::move(next_faders);
    assignments.bus_meters = std::move(next_meters);
    return true;
}

}  // namespace vocroz::engine
