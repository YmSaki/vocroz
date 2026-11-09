// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト用の MixerControl。ミキサーに反映された最新の値を保持し、テストが「音が出る状態か」を
// 確かめられるようにする。
#pragma once

#include <map>
#include <string>
#include <utility>

#include "vocroz/session/ports/mixer_control.h"

namespace vocroz::session {

/// ミキサーの状態を記録する MixerControl。
/// 初期状態は「Master もミュートされていない」とし、session が明示的にミュートしたことを確かめる。
class FakeMixerControl final : public MixerControl {
public:
    /// 1 チャンネルの反映済みの値。
    struct Channel {
        bool muted = true;
        control_protocol::GainDb gain_db = 0.0;
    };

    void add_channel(const std::string& strip_id) override {
        channels_[strip_id] = Channel{};
        ++add_count_;
    }

    void remove_channel(const std::string& strip_id) override {
        channels_.erase(strip_id);
    }

    void set_channel_muted(const std::string& strip_id, bool muted) override {
        channels_[strip_id].muted = muted;
    }

    void set_channel_gain(const std::string& strip_id, control_protocol::GainDb gain_db) override {
        channels_[strip_id].gain_db = gain_db;
    }

    void set_master_muted(bool muted) override {
        master_muted_ = muted;
    }

    void set_master_gain(control_protocol::GainDb gain_db) override {
        master_gain_db_ = gain_db;
    }

    bool apply_routing(const control_protocol::RoutingConfig& config,
                       bool feedback_approved) override {
        ++apply_routing_count_;
        if (!accept_routing_) {
            return false;
        }
        routing_ = config;
        feedback_approved_ = feedback_approved;
        // 新しいバスとセンドはミュートで 0 dB、残るものは値を引き継ぐ（ポートの契約）。
        std::map<std::string, Channel> routes;
        const auto keep = [&](const std::string& id) {
            const auto it = routes_.find(id);
            routes[id] = it == routes_.end() ? Channel{} : it->second;
        };
        for (const auto& bus : config.buses) {
            keep(bus.entity_id);
        }
        for (const auto& send : config.sends) {
            keep(send.entity_id);
        }
        routes_ = std::move(routes);
        return true;
    }

    void set_route_muted(const std::string& entity_id, bool muted) override {
        routes_[entity_id].muted = muted;
    }

    void set_route_gain(const std::string& entity_id, control_protocol::GainDb gain_db) override {
        routes_[entity_id].gain_db = gain_db;
    }

    /// apply_routing() が false を返すようにする（ミキサーが記述を受け付けない場合）。
    void reject_routing() noexcept {
        accept_routing_ = false;
    }

    /// 最後に適用したルーティングの構成。
    [[nodiscard]] const control_protocol::RoutingConfig& routing() const noexcept {
        return routing_;
    }

    [[nodiscard]] bool feedback_approved() const noexcept {
        return feedback_approved_;
    }

    [[nodiscard]] int apply_routing_count() const noexcept {
        return apply_routing_count_;
    }

    /// バスとセンドの反映済みの値。
    [[nodiscard]] const std::map<std::string, Channel>& routes() const noexcept {
        return routes_;
    }

    [[nodiscard]] const std::map<std::string, Channel>& channels() const noexcept {
        return channels_;
    }

    /// add_channel() が呼ばれた回数（ストリップの重複生成の検出に使う）。
    [[nodiscard]] int add_count() const noexcept {
        return add_count_;
    }

    [[nodiscard]] bool master_muted() const noexcept {
        return master_muted_;
    }

    [[nodiscard]] control_protocol::GainDb master_gain_db() const noexcept {
        return master_gain_db_;
    }

private:
    std::map<std::string, Channel> channels_;
    std::map<std::string, Channel> routes_;
    control_protocol::RoutingConfig routing_;
    bool feedback_approved_ = false;
    bool accept_routing_ = true;
    int apply_routing_count_ = 0;
    int add_count_ = 0;
    bool master_muted_ = false;
    control_protocol::GainDb master_gain_db_ = 0.0;
};

}  // namespace vocroz::session
