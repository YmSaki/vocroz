// SPDX-License-Identifier: AGPL-3.0-only
//
// establish_secure_link() の実装。

#include "vocroz/test_harness/secure_link.h"

#include <array>
#include <cstdint>
#include <utility>

#include "vocroz/transport/constants.h"
#include "vocroz/transport/noise_ik_handshake.h"
#include "vocroz/transport/static_key_pair.h"

namespace vocroz::test_harness {

using common::make_unexpected;
using transport::TransportError;

common::Result<SecureLink, TransportError> establish_secure_link(
    transport::ConnectionId connection_id) {
    auto client_keys = transport::StaticKeyPair::generate();
    auto host_keys = transport::StaticKeyPair::generate();
    if (!client_keys) {
        return make_unexpected(client_keys.error());
    }
    if (!host_keys) {
        return make_unexpected(host_keys.error());
    }
    auto initiator = transport::NoiseIkHandshake::create_initiator(
        client_keys.value(), host_keys.value().public_key(), connection_id);
    if (!initiator) {
        return make_unexpected(initiator.error());
    }
    auto responder = transport::NoiseIkHandshake::create_responder(host_keys.value());
    if (!responder) {
        return make_unexpected(responder.error());
    }
    std::array<std::uint8_t, transport::kMaxPacketBytes> packet{};
    std::array<std::uint8_t, transport::kMaxPacketBytes> payload{};
    const auto initiation = initiator.value().write_initiation({}, packet);
    if (!initiation) {
        return make_unexpected(initiation.error());
    }
    if (auto read =
            responder.value().read_initiation(std::span(packet).first(initiation.value()), payload);
        !read) {
        return make_unexpected(read.error());
    }
    const auto response = responder.value().write_response({}, packet);
    if (!response) {
        return make_unexpected(response.error());
    }
    if (auto read =
            initiator.value().read_response(std::span(packet).first(response.value()), payload);
        !read) {
        return make_unexpected(read.error());
    }
    auto client = initiator.value().finish();
    if (!client) {
        return make_unexpected(client.error());
    }
    auto host = responder.value().finish();
    if (!host) {
        return make_unexpected(host.error());
    }
    return SecureLink{.client = std::move(client).value(), .host = std::move(host).value()};
}

}  // namespace vocroz::test_harness
