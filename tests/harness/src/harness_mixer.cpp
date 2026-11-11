// SPDX-License-Identifier: AGPL-3.0-only
//
// HarnessMixer の実装。

#include "vocroz/test_harness/harness_mixer.h"

#include "vocroz/audio_core/gain.h"

namespace vocroz::test_harness {
namespace {

// ミュートとフェーダーの値からリニアゲインを求める。-∞ dB は無音。
float linear_gain(bool muted, const control_protocol::GainDb& gain_db) {
    if (muted || !gain_db) {
        return 0.0F;
    }
    return audio_core::fader_db_to_linear(static_cast<float>(*gain_db));
}

}  // namespace

void HarnessMixer::add_channel(const std::string& strip_id) {
    channels_.try_emplace(strip_id);
}

void HarnessMixer::remove_channel(const std::string& strip_id) {
    channels_.erase(strip_id);
}

void HarnessMixer::set_channel_muted(const std::string& strip_id, bool muted) {
    if (const auto found = channels_.find(strip_id); found != channels_.end()) {
        found->second.muted = muted;
    }
}

void HarnessMixer::set_channel_gain(const std::string& strip_id, control_protocol::GainDb gain_db) {
    if (const auto found = channels_.find(strip_id); found != channels_.end()) {
        found->second.gain_db = gain_db;
    }
}

void HarnessMixer::set_master_muted(bool muted) {
    master_muted_ = muted;
}

void HarnessMixer::set_master_gain(control_protocol::GainDb gain_db) {
    master_gain_db_ = gain_db;
}

bool HarnessMixer::apply_routing(const control_protocol::RoutingConfig& /*config*/,
                                 bool /*feedback_approved*/,
                                 std::span<const session::InsertedPlugin> /*plugins*/) {
    return true;
}

void HarnessMixer::set_route_muted(const std::string& /*entity_id*/, bool /*muted*/) {}

void HarnessMixer::set_route_gain(const std::string& /*entity_id*/,
                                  control_protocol::GainDb /*gain_db*/) {}

void HarnessMixer::accumulate(const std::string& strip_id, std::span<const float> input,
                              std::span<float> out) const {
    const auto found = channels_.find(strip_id);
    if (found == channels_.end()) {
        return;
    }
    const float gain = linear_gain(found->second.muted, found->second.gain_db);
    for (std::size_t i = 0; i < out.size() && i < input.size(); ++i) {
        out[i] += input[i] * gain;
    }
}

void HarnessMixer::apply_master(std::span<float> out) const {
    const float gain = linear_gain(master_muted_, master_gain_db_);
    for (float& sample : out) {
        sample *= gain;
    }
}

}  // namespace vocroz::test_harness
