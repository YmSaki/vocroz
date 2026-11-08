// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト用の GatewayChannel。テストが用意した応答を順に返し、送られた要求を記録する。
// 応答がなくなったら、時間切れ（kTimedOut）を返す。
#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <vector>

#include "vocroz/network/ports/gateway_channel.h"

namespace vocroz::network {

/// 用意した応答を返す GatewayChannel。
class FakeGatewayChannel final : public GatewayChannel {
public:
    /// 1 回の exchange() の結果。bytes が空なら error を返す。
    struct Reply {
        std::vector<std::uint8_t> bytes;
        NetworkError error = NetworkError::kTimedOut;
    };

    [[nodiscard]] UdpAddress local_address() const noexcept override {
        return local;
    }

    [[nodiscard]] common::Result<std::size_t, NetworkError> exchange(
        std::span<const std::uint8_t> request, std::span<std::uint8_t> response,
        std::chrono::milliseconds timeout) override {
        requests.emplace_back(request.begin(), request.end());
        timeouts.push_back(timeout);
        if (replies.empty()) {
            return common::make_unexpected(NetworkError::kTimedOut);
        }
        const Reply reply = replies.front();
        replies.pop_front();
        if (reply.bytes.empty()) {
            return common::make_unexpected(reply.error);
        }
        std::copy(reply.bytes.begin(), reply.bytes.end(), response.begin());
        return reply.bytes.size();
    }

    /// local_address() が返す値。
    UdpAddress local = UdpAddress::from_ipv4({192, 168, 1, 10}, 0);
    /// 順に返す応答。
    std::deque<Reply> replies;
    /// 送られた要求（古い順）。
    std::vector<std::vector<std::uint8_t>> requests;
    /// exchange() に渡された待ち時間（古い順）。
    std::vector<std::chrono::milliseconds> timeouts;
};

}  // namespace vocroz::network
