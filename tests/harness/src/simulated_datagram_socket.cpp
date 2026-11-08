// SPDX-License-Identifier: AGPL-3.0-only
//
// SimulatedDatagramSocket の実装。

#include "vocroz/test_harness/simulated_datagram_socket.h"

namespace vocroz::test_harness {

common::Result<void, transport::TransportError> SimulatedDatagramSocket::send(
    std::span<const std::uint8_t> datagram) noexcept {
    // SimulatedNetwork::send() はコピーのためにヒープを確保する。テスト専用なので、
    // 確保の失敗（std::bad_alloc）はテストの失敗として扱い、ここでは捕まえない。
    network_.send(local_, remote_, datagram);
    return {};
}

}  // namespace vocroz::test_harness
