// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト用の MixerControl。ミキサーに反映された最新の値を保持し、テストが「音が出る状態か」を
// 確かめられるようにする。
#pragma once

#include <map>
#include <string>

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
    int add_count_ = 0;
    bool master_muted_ = false;
    control_protocol::GainDb master_gain_db_ = 0.0;
};

}  // namespace vocroz::session
