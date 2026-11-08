// SPDX-License-Identifier: AGPL-3.0-only
//
// 暗号化済みパケットの作成と検証。AEAD には libsodium の ChaCha20-Poly1305（IETF）を使う。
// Noise のハンドシェイク後の通信は、本来は Noise の CipherState が nonce を暗黙に数えて使うが、
// UDP ではパケットが欠落したり入れ替わったりする。
// そこで、シーケンス番号をヘッダーに明示して nonce に使う。

#include "vocroz/transport/secure_channel.h"

#include <cstring>
#include <limits>

#include <sodium.h>

#include "transport_constants.h"
#include "wire/media_payload_codec.h"
#include "wire/packet_header_codec.h"

namespace vocroz::transport {
namespace {

static_assert(crypto_aead_chacha20poly1305_ietf_KEYBYTES == kTransportKeyBytes);
static_assert(crypto_aead_chacha20poly1305_ietf_ABYTES == kAeadTagBytes);
static_assert(crypto_aead_chacha20poly1305_ietf_NPUBBYTES == kAeadNonceBytes);

// 窓の 1 語のビット数。
constexpr std::size_t kBitsPerWord = 64;

// Noise の nonce の上限（2^64 - 1）は予約値なので、送信には使わない。
constexpr std::uint64_t kReservedSequence = std::numeric_limits<std::uint64_t>::max();

// シーケンス番号から AEAD の nonce を作る。Noise の ChaChaPoly と同じく、
// 先頭 4 バイトを 0 にし、後ろ 8 バイトにシーケンス番号をリトルエンディアンで置く。
[[nodiscard]] std::array<std::uint8_t, kAeadNonceBytes> make_nonce(
    std::uint64_t sequence) noexcept {
    std::array<std::uint8_t, kAeadNonceBytes> nonce{};
    for (std::size_t i = 0; i < sizeof(sequence); ++i) {
        nonce[4 + i] = static_cast<std::uint8_t>((sequence >> (i * 8)) & 0xFFU);
    }
    return nonce;
}

[[nodiscard]] bool is_encrypted_type(PacketType type) noexcept {
    switch (type) {
        case PacketType::kMedia:
        case PacketType::kControl:
            return true;
        case PacketType::kHandshakeInitiation:
        case PacketType::kHandshakeResponse:
            return false;
    }
    return false;
}

}  // namespace

// ---- PacketSealer ----

PacketSealer::PacketSealer(ConnectionId connection_id,
                           const std::array<std::uint8_t, kTransportKeyBytes>& key) noexcept
    : connection_id_(connection_id), key_(key), has_key_(true) {}

PacketSealer::PacketSealer(PacketSealer&& other) noexcept
    : connection_id_(other.connection_id_),
      key_(other.key_),
      next_sequence_(other.next_sequence_),
      has_key_(other.has_key_) {
    sodium_memzero(other.key_.data(), other.key_.size());
    other.has_key_ = false;
}

PacketSealer& PacketSealer::operator=(PacketSealer&& other) noexcept {
    if (this != &other) {
        connection_id_ = other.connection_id_;
        key_ = other.key_;
        next_sequence_ = other.next_sequence_;
        has_key_ = other.has_key_;
        sodium_memzero(other.key_.data(), other.key_.size());
        other.has_key_ = false;
    }
    return *this;
}

PacketSealer::~PacketSealer() {
    sodium_memzero(key_.data(), key_.size());
}

common::Result<std::size_t, TransportError> PacketSealer::seal_media(
    const MediaPayload& payload, std::span<std::uint8_t> out) noexcept {
    // DTX を使わないので空のフレームは送らない（受信側も不正として捨てる）。
    if (payload.opus_payload.empty()) {
        return common::make_unexpected(TransportError::kMalformedPayload);
    }
    const std::size_t plaintext_bytes = wire::media_payload_bytes(payload);
    if (auto checked = check_can_seal(plaintext_bytes, out.size()); !checked) {
        return common::make_unexpected(checked.error());
    }
    wire::write_media_payload(payload, out.subspan(kPacketHeaderBytes));
    return seal_in_place(PacketType::kMedia, plaintext_bytes, out);
}

common::Result<std::size_t, TransportError> PacketSealer::seal_control(
    std::span<const std::uint8_t> plaintext, std::span<std::uint8_t> out) noexcept {
    if (auto checked = check_can_seal(plaintext.size(), out.size()); !checked) {
        return common::make_unexpected(checked.error());
    }
    if (!plaintext.empty()) {
        // 呼び出し側が out の中に平文を置いていても壊さないよう、重なりを許す memmove で写す。
        std::memmove(out.data() + kPacketHeaderBytes, plaintext.data(), plaintext.size());
    }
    return seal_in_place(PacketType::kControl, plaintext.size(), out);
}

common::Result<void, TransportError> PacketSealer::check_can_seal(
    std::size_t plaintext_bytes, std::size_t out_bytes) const noexcept {
    if (!has_key_) {
        return common::make_unexpected(TransportError::kInvalidState);
    }
    if (next_sequence_ == kReservedSequence) {
        return common::make_unexpected(TransportError::kNonceExhausted);
    }
    if (plaintext_bytes > kMaxPlaintextBytes) {
        return common::make_unexpected(TransportError::kPayloadTooLarge);
    }
    if (out_bytes < sealed_packet_bytes(plaintext_bytes)) {
        return common::make_unexpected(TransportError::kBufferTooSmall);
    }
    return {};
}

common::Result<std::size_t, TransportError> PacketSealer::seal_in_place(
    PacketType type, std::size_t plaintext_bytes, std::span<std::uint8_t> out) noexcept {
    const std::uint64_t sequence = next_sequence_;
    wire::write_packet_header(
        PacketHeader{.type = type, .connection_id = connection_id_, .sequence = sequence}, out);
    const auto nonce = make_nonce(sequence);
    std::uint8_t* const body = out.data() + kPacketHeaderBytes;
    unsigned long long ciphertext_bytes = 0;
    // libsodium は平文と暗号文が同じ領域でもよいので、その場で暗号化する（コピーを避ける）。
    // ヘッダー全体を関連データにして、種別、接続 ID、シーケンス番号の書き換えを検出する。
    crypto_aead_chacha20poly1305_ietf_encrypt(body, &ciphertext_bytes, body, plaintext_bytes,
                                              out.data(), kPacketHeaderBytes, nullptr, nonce.data(),
                                              key_.data());
    ++next_sequence_;
    return kPacketHeaderBytes + static_cast<std::size_t>(ciphertext_bytes);
}

// ---- PacketOpener ----

PacketOpener::PacketOpener(ConnectionId connection_id,
                           const std::array<std::uint8_t, kTransportKeyBytes>& key) noexcept
    : connection_id_(connection_id), key_(key), has_key_(true) {}

PacketOpener::PacketOpener(PacketOpener&& other) noexcept
    : connection_id_(other.connection_id_),
      key_(other.key_),
      highest_sequence_(other.highest_sequence_),
      window_(other.window_),
      has_received_(other.has_received_),
      has_key_(other.has_key_) {
    sodium_memzero(other.key_.data(), other.key_.size());
    other.has_key_ = false;
}

PacketOpener& PacketOpener::operator=(PacketOpener&& other) noexcept {
    if (this != &other) {
        connection_id_ = other.connection_id_;
        key_ = other.key_;
        highest_sequence_ = other.highest_sequence_;
        window_ = other.window_;
        has_received_ = other.has_received_;
        has_key_ = other.has_key_;
        sodium_memzero(other.key_.data(), other.key_.size());
        other.has_key_ = false;
    }
    return *this;
}

PacketOpener::~PacketOpener() {
    sodium_memzero(key_.data(), key_.size());
}

common::Result<OpenedPacket, TransportError> PacketOpener::open(
    std::span<const std::uint8_t> packet, std::span<std::uint8_t> plaintext_out) noexcept {
    if (!has_key_) {
        return common::make_unexpected(TransportError::kInvalidState);
    }
    const auto header = parse_packet_header(packet);
    if (!header) {
        return common::make_unexpected(header.error());
    }
    if (!is_encrypted_type(header.value().type)) {
        return common::make_unexpected(TransportError::kUnexpectedPacketType);
    }
    if (header.value().connection_id != connection_id_) {
        return common::make_unexpected(TransportError::kConnectionMismatch);
    }
    if (packet.size() < sealed_packet_bytes(0)) {
        return common::make_unexpected(TransportError::kTruncated);
    }
    const std::size_t plaintext_bytes = packet.size() - sealed_packet_bytes(0);
    if (plaintext_out.size() < plaintext_bytes) {
        return common::make_unexpected(TransportError::kBufferTooSmall);
    }
    const std::uint64_t sequence = header.value().sequence;
    // 復号より先に窓で判定し、リプレイには復号の計算資源を使わない。
    // 窓への記録は認証の成功後に行う（偽造パケットで窓を進めさせないため）。
    if (!is_acceptable(sequence)) {
        return common::make_unexpected(TransportError::kReplayed);
    }
    const auto nonce = make_nonce(sequence);
    unsigned long long decrypted_bytes = 0;
    const int status = crypto_aead_chacha20poly1305_ietf_decrypt(
        plaintext_out.data(), &decrypted_bytes, nullptr, packet.data() + kPacketHeaderBytes,
        packet.size() - kPacketHeaderBytes, packet.data(), kPacketHeaderBytes, nonce.data(),
        key_.data());
    if (status != 0) {
        return common::make_unexpected(TransportError::kAuthenticationFailed);
    }
    mark_received(sequence);
    return OpenedPacket{
        .type = header.value().type,
        .sequence = sequence,
        .plaintext = plaintext_out.first(static_cast<std::size_t>(decrypted_bytes)),
    };
}

bool PacketOpener::is_acceptable(std::uint64_t sequence) const noexcept {
    if (sequence == kReservedSequence) {
        return false;
    }
    if (!has_received_ || sequence > highest_sequence_) {
        return true;
    }
    const std::uint64_t age = highest_sequence_ - sequence;
    if (age >= kReplayWindowPackets) {
        return false;
    }
    const auto bit = static_cast<std::size_t>(age);
    return ((window_[bit / kBitsPerWord] >> (bit % kBitsPerWord)) & 1U) == 0;
}

void PacketOpener::mark_received(std::uint64_t sequence) noexcept {
    if (!has_received_ || sequence > highest_sequence_) {
        // 最新の番号が進んだ分だけ窓をずらす（ビット i を i + shift へ移す）。
        const std::uint64_t advance = has_received_ ? sequence - highest_sequence_ : 0;
        if (!has_received_ || advance >= kReplayWindowPackets) {
            window_.fill(0);
        } else {
            const auto shift = static_cast<std::size_t>(advance);
            const std::size_t word_shift = shift / kBitsPerWord;
            const std::size_t bit_shift = shift % kBitsPerWord;
            for (std::size_t w = kWindowWords; w-- > 0;) {
                std::uint64_t moved = 0;
                if (w >= word_shift) {
                    moved = window_[w - word_shift] << bit_shift;
                    if (bit_shift != 0 && w >= word_shift + 1) {
                        moved |= window_[w - word_shift - 1] >> (kBitsPerWord - bit_shift);
                    }
                }
                window_[w] = moved;
            }
        }
        highest_sequence_ = sequence;
        has_received_ = true;
        window_[0] |= 1U;
        return;
    }
    const auto bit = static_cast<std::size_t>(highest_sequence_ - sequence);
    window_[bit / kBitsPerWord] |= std::uint64_t{1} << (bit % kBitsPerWord);
}

}  // namespace vocroz::transport
