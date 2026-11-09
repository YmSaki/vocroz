// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト用の UdpEndpoint。送られたデータグラムを宛先とともに記録するだけで、どこにも送らない。
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "vocroz/network/ports/udp_endpoint.h"

namespace vocroz::network {

/// 送られたデータグラムを記録する UdpEndpoint。
class FakeUdpEndpoint final : public UdpEndpoint {
public:
    /// 送られたデータグラム 1 つ。
    struct Sent {
        UdpAddress to;
        std::vector<std::uint8_t> datagram;
    };

    /// データグラムを記録する。fail_sends が true なら記録せずに kSendFailed を返す。
    [[nodiscard]] common::Result<void, NetworkError> send_to(
        const UdpAddress& to, std::span<const std::uint8_t> datagram) noexcept override {
        if (fail_sends) {
            return common::make_unexpected(NetworkError::kSendFailed);
        }
        sent.push_back(Sent{to, {datagram.begin(), datagram.end()}});
        return {};
    }

    [[nodiscard]] std::uint16_t local_port() const noexcept override {
        return port;
    }

    /// 宛先が to のデータグラムの数を返す。
    [[nodiscard]] std::size_t count_to(const UdpAddress& to) const noexcept {
        std::size_t n = 0;
        for (const Sent& s : sent) {
            n += s.to == to ? 1 : 0;
        }
        return n;
    }

    /// 送られたデータグラム（古い順）。
    std::vector<Sent> sent;
    /// local_port() が返す値。
    std::uint16_t port = 40000;
    /// true の間は送信を失敗させる。
    bool fail_sends = false;
};

}  // namespace vocroz::network
