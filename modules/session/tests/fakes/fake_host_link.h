// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト用の HostLink。接続の要求と、送った制御メッセージを記録する。
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "vocroz/session/ports/host_link.h"

namespace vocroz::session {

/// 子機側の接続の操作を記録する HostLink。
class FakeHostLink final : public HostLink {
public:
    void connect(const HostEndpoint& endpoint) override {
        connected_to_ = endpoint;
        open_ = true;
    }

    void reconnect() override {
        ++reconnect_count_;
    }

    void send(std::span<const std::uint8_t> message) override {
        sent_.emplace_back(message.begin(), message.end());
    }

    void close() override {
        open_ = false;
        ++close_count_;
    }

    /// 最後に connect() した接続先。
    [[nodiscard]] const std::optional<HostEndpoint>& connected_to() const noexcept {
        return connected_to_;
    }

    /// connect() の後、close() されていなければ true。
    [[nodiscard]] bool open() const noexcept {
        return open_;
    }

    [[nodiscard]] int reconnect_count() const noexcept {
        return reconnect_count_;
    }

    [[nodiscard]] int close_count() const noexcept {
        return close_count_;
    }

    /// 送ったメッセージ（送った順）。
    [[nodiscard]] const std::vector<std::vector<std::uint8_t>>& sent() const noexcept {
        return sent_;
    }

    void clear_sent() noexcept {
        sent_.clear();
    }

private:
    std::optional<HostEndpoint> connected_to_;
    bool open_ = false;
    int reconnect_count_ = 0;
    int close_count_ = 0;
    std::vector<std::vector<std::uint8_t>> sent_;
};

}  // namespace vocroz::session
