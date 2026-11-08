// SPDX-License-Identifier: AGPL-3.0-only
//
// ランデブーメッセージのシリアライズとパース。

#include "wire/rendezvous_codec.h"

#include <algorithm>

#include "vocroz/network/address_selection.h"

#include "detail/byte_cursor.h"

namespace vocroz::network::wire {

namespace {

// ワイヤーフォーマットのバージョン。サーバーの protocolVersion と同じ値。
constexpr std::uint8_t kVersion = 1;

constexpr std::array<std::uint8_t, 3> kMagic = {'V', 'R', 'Z'};

enum class MessageType : std::uint8_t {
    kRegister = 1,
    kRegistered = 2,
    kPunch = 3,
};

constexpr std::uint8_t kFamilyIpv4 = 4;
constexpr std::uint8_t kFamilyIpv6 = 6;

// REGISTERED の status。0 が成功、1 がサーバーの上限による拒否。
constexpr std::uint8_t kStatusOk = 0;
constexpr std::uint8_t kStatusCapacity = 1;

// REGISTERED の flags の bit 0。相手の役割の登録がすでにある。
constexpr std::uint8_t kFlagPeerPresent = 0x01;

void write_candidate(detail::ByteWriter& writer, const UdpAddress& address) noexcept {
    if (address.is_ipv4()) {
        writer.u8(kFamilyIpv4);
        writer.u16(address.port);
        const Ipv4Bytes v4 = address.ipv4();
        writer.bytes(v4);
    } else {
        writer.u8(kFamilyIpv6);
        writer.u16(address.port);
        writer.bytes(address.ip);
    }
}

// 候補を 1 つ読む。未指定アドレス、マルチキャスト、ポート 0 は不正とする。
// サーバーと同じ規則で、受け取った候補で関係のない宛先へ送らされないようにするため。
[[nodiscard]] UdpAddress read_candidate(detail::ByteReader& reader) noexcept {
    const std::uint8_t family = reader.u8();
    const std::uint16_t port = reader.u16();
    UdpAddress address;
    if (family == kFamilyIpv4) {
        const auto b = reader.take(4);
        if (!b.empty()) {
            address = UdpAddress::from_ipv4({b[0], b[1], b[2], b[3]}, port);
        }
    } else if (family == kFamilyIpv6) {
        const auto b = reader.take(16);
        if (!b.empty()) {
            Ipv6Bytes ip{};
            std::copy(b.begin(), b.end(), ip.begin());
            address = UdpAddress::from_ipv6(ip, port);
        }
    } else {
        reader.fail();
    }
    if (reader.ok() && !is_routable_candidate(address)) {
        reader.fail();
    }
    return address;
}

template <std::size_t N>
void read_fixed(detail::ByteReader& reader, std::array<std::uint8_t, N>& out) noexcept {
    const auto b = reader.take(N);
    if (!b.empty()) {
        std::copy(b.begin(), b.end(), out.begin());
    }
}

// 共通ヘッダーを読み、種別が want なら txid を返す。違えば reader を失敗にする。
[[nodiscard]] TxId read_header(detail::ByteReader& reader, MessageType want) noexcept {
    const auto magic = reader.take(kMagic.size());
    const std::uint8_t version = reader.u8();
    const std::uint8_t type = reader.u8();
    TxId tx_id{};
    read_fixed(reader, tx_id);
    if (!reader.ok() || !std::equal(magic.begin(), magic.end(), kMagic.begin()) ||
        version != kVersion || type != static_cast<std::uint8_t>(want)) {
        reader.fail();
    }
    return tx_id;
}

[[nodiscard]] RendezvousRole read_role(detail::ByteReader& reader) noexcept {
    const std::uint8_t role = reader.u8();
    if (role == static_cast<std::uint8_t>(RendezvousRole::kHost)) {
        return RendezvousRole::kHost;
    }
    if (role != static_cast<std::uint8_t>(RendezvousRole::kClient)) {
        reader.fail();
    }
    return RendezvousRole::kClient;
}

}  // namespace

bool is_rendezvous_message(std::span<const std::uint8_t> datagram) noexcept {
    return datagram.size() >= kRendezvousHeaderBytes &&
           std::equal(kMagic.begin(), kMagic.end(), datagram.begin());
}

common::Result<std::size_t, NetworkError> encode_register(const RegisterMessage& message,
                                                          std::span<std::uint8_t> out) noexcept {
    if (message.candidates.size() > kMaxLocalCandidates ||
        !std::all_of(message.candidates.begin(), message.candidates.end(),
                     [](const UdpAddress& a) { return is_routable_candidate(a); })) {
        return common::make_unexpected(NetworkError::kSendFailed);
    }
    detail::ByteWriter writer(out);
    writer.bytes(kMagic);
    writer.u8(kVersion);
    writer.u8(static_cast<std::uint8_t>(MessageType::kRegister));
    writer.bytes(message.tx_id);
    writer.bytes(message.rendezvous_id);
    writer.bytes(message.peer_id);
    writer.u8(static_cast<std::uint8_t>(message.role));
    writer.u8(static_cast<std::uint8_t>(message.candidates.size()));
    for (const UdpAddress& candidate : message.candidates) {
        write_candidate(writer, candidate);
    }
    if (!writer.ok()) {
        return common::make_unexpected(NetworkError::kSendFailed);
    }
    return writer.size();
}

common::Result<RegisteredMessage, NetworkError> parse_registered(
    std::span<const std::uint8_t> datagram) noexcept {
    detail::ByteReader reader(datagram);
    RegisteredMessage message;
    message.tx_id = read_header(reader, MessageType::kRegistered);
    const std::uint8_t status = reader.u8();
    const std::uint8_t flags = reader.u8();
    if (status != kStatusOk && status != kStatusCapacity) {
        reader.fail();
    }
    message.capacity_exceeded = status == kStatusCapacity;
    message.peer_present = (flags & kFlagPeerPresent) != 0;
    message.reflexive = read_candidate(reader);
    if (!reader.finished()) {
        return common::make_unexpected(NetworkError::kMalformedResponse);
    }
    return message;
}

common::Result<PunchMessage, NetworkError> parse_punch(std::span<const std::uint8_t> datagram) {
    detail::ByteReader reader(datagram);
    PunchMessage message;
    message.tx_id = read_header(reader, MessageType::kPunch);
    read_fixed(reader, message.rendezvous_id);
    read_fixed(reader, message.peer_id);
    message.peer_role = read_role(reader);
    const std::size_t count = reader.u8();
    if (count > kMaxLocalCandidates + 1) {
        reader.fail();
    }
    for (std::size_t i = 0; i < count && reader.ok(); ++i) {
        message.candidates.push_back(read_candidate(reader));
    }
    if (!reader.finished()) {
        return common::make_unexpected(NetworkError::kMalformedResponse);
    }
    return message;
}

}  // namespace vocroz::network::wire
