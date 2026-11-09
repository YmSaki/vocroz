// SPDX-License-Identifier: AGPL-3.0-only
//
// テストで使う、鍵交換を済ませた子機側と親機側の SecureChannel の組を作るヘルパー。
// 子機が送ったパケットを、親機側の opener で復号して中身を確かめるために使う。
#pragma once

#include <cstdlib>
#include <utility>
#include <vector>

#include "vocroz/transport/noise_ik_handshake.h"
#include "vocroz/transport/secure_channel.h"
#include "vocroz/transport/static_key_pair.h"

namespace vocroz::client::test {

/// 鍵を作る。作れない環境ではテストを続けられないので止める。
inline transport::StaticKeyPair make_keys() {
    auto keys = transport::StaticKeyPair::generate();
    if (!keys) {
        std::abort();
    }
    return std::move(keys).value();
}

/// 鍵交換を済ませた組。
struct SecurePair {
    transport::SecureChannel client;
    transport::SecureChannel host;
};

/// Noise_IK を最後まで行い、子機側と親機側の SecureChannel を返す。
inline SecurePair make_secure_pair() {
    const auto client_keys = make_keys();
    const auto host_keys = make_keys();
    auto initiator =
        transport::NoiseIkHandshake::create_initiator(client_keys, host_keys.public_key(), 0x1234)
            .value();
    auto responder = transport::NoiseIkHandshake::create_responder(host_keys).value();
    std::vector<std::uint8_t> packet(transport::kMaxPacketBytes);
    std::vector<std::uint8_t> payload(transport::kMaxPacketBytes);
    const auto first = initiator.write_initiation({}, packet).value();
    static_cast<void>(responder.read_initiation(std::span(packet).first(first), payload).value());
    const auto second = responder.write_response({}, packet).value();
    static_cast<void>(initiator.read_response(std::span(packet).first(second), payload).value());
    return SecurePair{initiator.finish().value(), responder.finish().value()};
}

}  // namespace vocroz::client::test
