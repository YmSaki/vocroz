// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト用の DatagramSocket。送られたデータグラムを記録するだけで、どこにも送らない。
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "vocroz/transport/ports/datagram_socket.h"

namespace vocroz::transport {

/// 送られたデータグラムを記録する DatagramSocket。
class FakeDatagramSocket final : public DatagramSocket {
public:
    /// データグラムを記録する。fail_sends が true なら記録せずに kSendFailed を返す。
    [[nodiscard]] common::Result<void, TransportError> send(
        std::span<const std::uint8_t> datagram) noexcept override {
        if (fail_sends) {
            return common::make_unexpected(TransportError::kSendFailed);
        }
        datagrams.emplace_back(datagram.begin(), datagram.end());
        return {};
    }

    /// 送られたデータグラム（古い順）。
    std::vector<std::vector<std::uint8_t>> datagrams;
    /// true の間は送信を失敗させる。
    bool fail_sends = false;
};

}  // namespace vocroz::transport
