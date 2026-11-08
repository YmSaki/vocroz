// SPDX-License-Identifier: AGPL-3.0-only
//
// 制御パケットを暗号化して送る ControlPacketSink の実装。

#include "vocroz/transport/secure_control_packet_sink.h"

#include <array>

#include "vocroz/transport/constants.h"

namespace vocroz::transport {

common::Result<void, TransportError> SecureControlPacketSink::send_control_packet(
    std::span<const std::uint8_t> plaintext) noexcept {
    std::array<std::uint8_t, kMaxPacketBytes> packet{};
    const auto sealed = sealer_.seal_control(plaintext, packet);
    if (!sealed) {
        return common::make_unexpected(sealed.error());
    }
    return socket_.send(std::span(packet).first(sealed.value()));
}

}  // namespace vocroz::transport
