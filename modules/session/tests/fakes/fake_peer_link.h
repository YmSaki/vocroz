// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト用の PeerLink。送った制御メッセージと、接続の受け付けと切断を記録する。
#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "vocroz/session/ports/peer_link.h"

namespace vocroz::session {

/// 親機側の接続の操作を記録する PeerLink。
class FakePeerLink final : public PeerLink {
public:
    /// apply_send_mute() で渡された値。
    struct SendMute {
        bool send_muted = true;
        std::uint32_t epoch = 0;
    };

    void send(PeerHandle peer, std::span<const std::uint8_t> message) override {
        sent_[peer].emplace_back(message.begin(), message.end());
    }

    void admit(PeerHandle peer, const std::string& strip_id) override {
        admitted_[peer] = strip_id;
    }

    void apply_send_mute(PeerHandle peer, bool send_muted, std::uint32_t epoch) override {
        send_mutes_[peer] = SendMute{send_muted, epoch};
    }

    void close(PeerHandle peer) override {
        closed_.insert(peer);
        admitted_.erase(peer);
    }

    /// 接続 peer に送ったメッセージ（送った順）。
    [[nodiscard]] std::vector<std::vector<std::uint8_t>> sent_to(PeerHandle peer) const {
        const auto it = sent_.find(peer);
        return it == sent_.end() ? std::vector<std::vector<std::uint8_t>>{} : it->second;
    }

    /// 受け付けている接続と、流し込み先のストリップ。
    [[nodiscard]] const std::map<PeerHandle, std::string>& admitted() const noexcept {
        return admitted_;
    }

    [[nodiscard]] bool closed(PeerHandle peer) const {
        return closed_.contains(peer);
    }

    [[nodiscard]] const std::map<PeerHandle, SendMute>& send_mutes() const noexcept {
        return send_mutes_;
    }

private:
    std::map<PeerHandle, std::vector<std::vector<std::uint8_t>>> sent_;
    std::map<PeerHandle, std::string> admitted_;
    std::map<PeerHandle, SendMute> send_mutes_;
    std::set<PeerHandle> closed_;
};

}  // namespace vocroz::session
