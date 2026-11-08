// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト用の ControlPacketSink。送られた平文を順に記録するだけで、どこにも送らない。
// テストが記録から取り出して相手に渡すか、渡さない（欠落）か、二度渡す（重複）かを決めるので、
// 経路上の欠落、重複、順序の入れ替わりを決定的に再現できる。
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "vocroz/transport/ports/control_packet_sink.h"

namespace vocroz::transport {

/// 送られた平文を記録する ControlPacketSink。
class FakeControlPacketSink final : public ControlPacketSink {
public:
    /// 平文を記録する。fail_sends が true なら記録せずに kSendFailed を返す。
    [[nodiscard]] common::Result<void, TransportError> send_control_packet(
        std::span<const std::uint8_t> plaintext) noexcept override {
        if (fail_sends) {
            return common::make_unexpected(TransportError::kSendFailed);
        }
        packets.emplace_back(plaintext.begin(), plaintext.end());
        return {};
    }

    /// 記録した平文を古い順に取り出す。
    [[nodiscard]] std::vector<std::vector<std::uint8_t>> take() {
        auto taken = std::move(packets);
        packets.clear();
        return taken;
    }

    /// 送られた平文（古い順）。
    std::vector<std::vector<std::uint8_t>> packets;
    /// true の間は送信を失敗させる。
    bool fail_sends = false;
};

}  // namespace vocroz::transport
