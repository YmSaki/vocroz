// SPDX-License-Identifier: AGPL-3.0-only
//
// noise-c による Noise_IK のハンドシェイクと、ハンドシェイク後の送受信鍵の導出。

#include "vocroz/transport/noise_ik_handshake.h"

#include <algorithm>
#include <array>
#include <new>
#include <optional>
#include <utility>

#include <noise/protocol.h>
#include <sodium.h>

#include "detail/byte_order_utils.h"
#include "detail/crypto_utils.h"
#include "state/handshake_state_machine.h"
#include "transport_constants.h"
#include "wire/packet_header_codec.h"

namespace vocroz::transport {
namespace {

using state::HandshakeEvent;
using state::HandshakeState;
using TransportKey = std::array<std::uint8_t, kTransportKeyBytes>;

// プロローグの長さ: ラベル、バージョン（1）、接続 ID（4）。
constexpr std::size_t kPrologueBytes = kPrologueLabel.size() + 1 + sizeof(ConnectionId);
using Prologue = std::array<std::uint8_t, kPrologueBytes>;

// ラベル、バージョン、接続 ID
// からプロローグを作る。両側が同じ値をハンドシェイクのハッシュに混ぜるので、
// どれかが食い違えば、ハンドシェイクは鍵の不一致として失敗する。
[[nodiscard]] Prologue make_prologue(ConnectionId connection_id) noexcept {
    Prologue prologue{};
    std::copy(kPrologueLabel.begin(), kPrologueLabel.end(), prologue.begin());
    prologue[kPrologueLabel.size()] = kProtocolVersion;
    detail::store_big_endian(connection_id,
                             std::span<std::uint8_t>(prologue).subspan(kPrologueLabel.size() + 1));
    return prologue;
}

// Noise の Split() で得た CipherState から、明示的な nonce で使う鍵を導出する。
// noise-c の CipherState は nonce を内部で 1 ずつ数え、巻き戻せないので、欠落と入れ替わりのある
// UDP のパケットをそのまま復号できない。また鍵そのものを取り出す API もない。
// そこで、その CipherState で nonce 0 のまま 32 バイトのゼロを暗号化し、暗号文を新しい鍵にする。
// これは Noise 仕様の REKEY と同じ考え方（ChaCha20 を鍵に対する擬似乱数関数として使う）で、
// 導出に使った CipherState はこの 1 回だけ使って破棄する。
// そのため、同じ鍵と nonce の組が二度使われることはない。
// 関連データは渡さない。関連データは認証タグにしか影響せず、捨てる側のタグには意味がないため。
// 送信鍵と受信鍵が別の値になるのは、Split() が方向ごとに別の鍵を返すことによる。
[[nodiscard]] bool derive_transport_key(NoiseCipherState* cipher, TransportKey& key) noexcept {
    std::array<std::uint8_t, kTransportKeyBytes + kAeadTagBytes> block{};
    NoiseBuffer buffer;
    noise_buffer_set_inout(buffer, block.data(), kTransportKeyBytes, block.size());
    const int status = noise_cipherstate_encrypt_with_ad(cipher, nullptr, 0, &buffer);
    if (status == NOISE_ERROR_NONE) {
        std::copy_n(block.begin(), kTransportKeyBytes, key.begin());
    }
    sodium_memzero(block.data(), block.size());
    return status == NOISE_ERROR_NONE;
}

// NoiseCipherState を確実に解放するための RAII ラッパー（AGENTS.md 6.1）。
class CipherStateHolder {
public:
    CipherStateHolder() = default;
    CipherStateHolder(const CipherStateHolder&) = delete;
    CipherStateHolder& operator=(const CipherStateHolder&) = delete;
    ~CipherStateHolder() {
        if (cipher_ != nullptr) {
            noise_cipherstate_free(cipher_);
        }
    }

    [[nodiscard]] NoiseCipherState** out() noexcept {
        return &cipher_;
    }

    [[nodiscard]] NoiseCipherState* get() const noexcept {
        return cipher_;
    }

private:
    NoiseCipherState* cipher_ = nullptr;
};

}  // namespace

struct NoiseIkHandshake::Impl {
    Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    ~Impl() {
        // noise-c は解放時に鍵の材料をゼロで上書きする。
        if (handshake != nullptr) {
            noise_handshakestate_free(handshake);
        }
    }

    // event を受け付けるかだけを確かめる。状態は変えない。
    [[nodiscard]] bool accepts(HandshakeEvent event) const noexcept {
        return state::next_state(stage, event).has_value();
    }

    // event を起こして状態を進める。前提条件: accepts(event) が true。
    void advance(HandshakeEvent event) noexcept {
        stage = state::next_state(stage, event).value_or(HandshakeState::kFailed);
    }

    // Noise の処理が失敗したときの共通処理。以降の呼び出しはすべて kInvalidState になる。
    [[nodiscard]] common::Unexpected<TransportError> fail() noexcept {
        advance(HandshakeEvent::kFailed);
        return common::make_unexpected(TransportError::kHandshakeFailed);
    }

    // プロローグを設定し、ハンドシェイクを開始する。
    [[nodiscard]] bool start(ConnectionId id) noexcept {
        connection_id = id;
        const Prologue prologue = make_prologue(id);
        return noise_handshakestate_set_prologue(handshake, prologue.data(), prologue.size()) ==
                   NOISE_ERROR_NONE &&
               noise_handshakestate_start(handshake) == NOISE_ERROR_NONE;
    }

    // 1 つのハンドシェイクメッセージを、ヘッダーを付けて out に書く。
    [[nodiscard]] common::Result<std::size_t, TransportError> write_message(
        PacketType type, HandshakeEvent event, std::size_t overhead, std::size_t max_payload,
        std::span<const std::uint8_t> payload, std::span<std::uint8_t> out) noexcept;

    // ヘッダーを検証し、1 つのハンドシェイクメッセージを読んで、ペイロードを payload_out に書く。
    // ヘッダーの不一致は、無関係なパケットが届いただけとみなし、状態を変えずにエラーを返す。
    [[nodiscard]] common::Result<std::size_t, TransportError> read_message(
        PacketType type, HandshakeEvent event, std::size_t overhead,
        std::span<const std::uint8_t> packet, std::span<std::uint8_t> payload_out) noexcept;

    NoiseHandshakeState* handshake = nullptr;
    HandshakeState stage = HandshakeState::kFailed;
    ConnectionId connection_id = 0;
    PublicKey remote_public_key{};
    bool has_remote_public_key = false;
};

common::Result<std::size_t, TransportError> NoiseIkHandshake::Impl::write_message(
    PacketType type, HandshakeEvent event, std::size_t overhead, std::size_t max_payload,
    std::span<const std::uint8_t> payload, std::span<std::uint8_t> out) noexcept {
    if (!accepts(event)) {
        return common::make_unexpected(TransportError::kInvalidState);
    }
    if (payload.size() > max_payload) {
        return common::make_unexpected(TransportError::kPayloadTooLarge);
    }
    if (out.size() < overhead + payload.size()) {
        return common::make_unexpected(TransportError::kBufferTooSmall);
    }
    NoiseBuffer message;
    noise_buffer_set_output(message, out.data() + kPacketHeaderBytes,
                            out.size() - kPacketHeaderBytes);
    NoiseBuffer payload_buffer;
    // noise-c の入力バッファは非 const のポインターを取るが、書き込みはしない。
    noise_buffer_set_input(payload_buffer, const_cast<std::uint8_t*>(payload.data()),
                           payload.size());
    // 空のペイロードは nullptr で渡す（noise-c はデータのポインターが null
    // のバッファを受け付けない）。
    if (noise_handshakestate_write_message(
            handshake, &message, payload.empty() ? nullptr : &payload_buffer) != NOISE_ERROR_NONE) {
        return fail();
    }
    wire::write_packet_header(
        PacketHeader{.type = type, .connection_id = connection_id, .sequence = 0}, out);
    advance(event);
    return kPacketHeaderBytes + message.size;
}

common::Result<std::size_t, TransportError> NoiseIkHandshake::Impl::read_message(
    PacketType type, HandshakeEvent event, std::size_t overhead,
    std::span<const std::uint8_t> packet, std::span<std::uint8_t> payload_out) noexcept {
    if (!accepts(event)) {
        return common::make_unexpected(TransportError::kInvalidState);
    }
    const auto header = parse_packet_header(packet);
    if (!header) {
        return common::make_unexpected(header.error());
    }
    if (header.value().type != type) {
        return common::make_unexpected(TransportError::kUnexpectedPacketType);
    }
    if (packet.size() < overhead) {
        return common::make_unexpected(TransportError::kTruncated);
    }
    if (payload_out.size() < packet.size() - overhead) {
        return common::make_unexpected(TransportError::kBufferTooSmall);
    }
    if (type == PacketType::kHandshakeInitiation) {
        // 応答側は、1 つ目のメッセージのヘッダーで初めて接続 ID を知る。
        if (!start(header.value().connection_id)) {
            return fail();
        }
    } else if (header.value().connection_id != connection_id) {
        return common::make_unexpected(TransportError::kConnectionMismatch);
    }
    NoiseBuffer message;
    // noise-c の入力バッファは非 const のポインターを取るが、書き込みはしない。
    noise_buffer_set_input(message, const_cast<std::uint8_t*>(packet.data()) + kPacketHeaderBytes,
                           packet.size() - kPacketHeaderBytes);
    NoiseBuffer payload_buffer;
    noise_buffer_set_output(payload_buffer, payload_out.data(), payload_out.size());
    if (noise_handshakestate_read_message(handshake, &message, &payload_buffer) !=
        NOISE_ERROR_NONE) {
        return fail();
    }
    if (type == PacketType::kHandshakeInitiation) {
        NoiseDHState* remote = noise_handshakestate_get_remote_public_key_dh(handshake);
        if (noise_dhstate_get_public_key(remote, remote_public_key.data(),
                                         remote_public_key.size()) != NOISE_ERROR_NONE) {
            return fail();
        }
        has_remote_public_key = true;
    }
    advance(event);
    return payload_buffer.size;
}

common::Result<ConnectionId, TransportError> generate_connection_id() noexcept {
    if (!detail::ensure_crypto_initialized()) {
        return common::make_unexpected(TransportError::kCryptoUnavailable);
    }
    return static_cast<ConnectionId>(randombytes_random());
}

common::Result<NoiseIkHandshake, TransportError> NoiseIkHandshake::create_initiator(
    const StaticKeyPair& local_keys, const PublicKey& responder_public,
    ConnectionId connection_id) noexcept {
    if (!detail::ensure_crypto_initialized()) {
        return common::make_unexpected(TransportError::kCryptoUnavailable);
    }
    auto impl = std::unique_ptr<Impl>(new (std::nothrow) Impl());
    if (!impl || noise_handshakestate_new_by_name(&impl->handshake, kNoiseProtocolName,
                                                  NOISE_ROLE_INITIATOR) != NOISE_ERROR_NONE) {
        return common::make_unexpected(TransportError::kCryptoUnavailable);
    }
    const auto private_key = local_keys.private_key();
    NoiseDHState* local = noise_handshakestate_get_local_keypair_dh(impl->handshake);
    NoiseDHState* remote = noise_handshakestate_get_remote_public_key_dh(impl->handshake);
    if (noise_dhstate_set_keypair_private(local, private_key.data(), private_key.size()) !=
            NOISE_ERROR_NONE ||
        noise_dhstate_set_public_key(remote, responder_public.data(), responder_public.size()) !=
            NOISE_ERROR_NONE ||
        !impl->start(connection_id)) {
        return common::make_unexpected(TransportError::kHandshakeFailed);
    }
    impl->remote_public_key = responder_public;
    impl->has_remote_public_key = true;
    impl->stage = HandshakeState::kAwaitingInitiationWrite;
    return NoiseIkHandshake(std::move(impl));
}

common::Result<NoiseIkHandshake, TransportError> NoiseIkHandshake::create_responder(
    const StaticKeyPair& local_keys) noexcept {
    if (!detail::ensure_crypto_initialized()) {
        return common::make_unexpected(TransportError::kCryptoUnavailable);
    }
    auto impl = std::unique_ptr<Impl>(new (std::nothrow) Impl());
    if (!impl || noise_handshakestate_new_by_name(&impl->handshake, kNoiseProtocolName,
                                                  NOISE_ROLE_RESPONDER) != NOISE_ERROR_NONE) {
        return common::make_unexpected(TransportError::kCryptoUnavailable);
    }
    const auto private_key = local_keys.private_key();
    NoiseDHState* local = noise_handshakestate_get_local_keypair_dh(impl->handshake);
    if (noise_dhstate_set_keypair_private(local, private_key.data(), private_key.size()) !=
        NOISE_ERROR_NONE) {
        return common::make_unexpected(TransportError::kHandshakeFailed);
    }
    // プロローグに接続 ID を入れるので、開始は 1 つ目のメッセージを読むときまで遅らせる。
    impl->stage = HandshakeState::kAwaitingInitiationRead;
    return NoiseIkHandshake(std::move(impl));
}

NoiseIkHandshake::NoiseIkHandshake(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}

NoiseIkHandshake::NoiseIkHandshake(NoiseIkHandshake&& other) noexcept = default;

NoiseIkHandshake& NoiseIkHandshake::operator=(NoiseIkHandshake&& other) noexcept = default;

NoiseIkHandshake::~NoiseIkHandshake() = default;

common::Result<std::size_t, TransportError> NoiseIkHandshake::write_initiation(
    std::span<const std::uint8_t> payload, std::span<std::uint8_t> out) noexcept {
    if (!impl_) {
        return common::make_unexpected(TransportError::kInvalidState);
    }
    return impl_->write_message(
        PacketType::kHandshakeInitiation, HandshakeEvent::kInitiationWritten,
        kHandshakeInitiationOverheadBytes, kMaxHandshakeInitiationPayloadBytes, payload, out);
}

common::Result<std::size_t, TransportError> NoiseIkHandshake::read_initiation(
    std::span<const std::uint8_t> packet, std::span<std::uint8_t> payload_out) noexcept {
    if (!impl_) {
        return common::make_unexpected(TransportError::kInvalidState);
    }
    return impl_->read_message(PacketType::kHandshakeInitiation, HandshakeEvent::kInitiationRead,
                               kHandshakeInitiationOverheadBytes, packet, payload_out);
}

common::Result<std::size_t, TransportError> NoiseIkHandshake::write_response(
    std::span<const std::uint8_t> payload, std::span<std::uint8_t> out) noexcept {
    if (!impl_) {
        return common::make_unexpected(TransportError::kInvalidState);
    }
    return impl_->write_message(PacketType::kHandshakeResponse, HandshakeEvent::kResponseWritten,
                                kHandshakeResponseOverheadBytes, kMaxHandshakeResponsePayloadBytes,
                                payload, out);
}

common::Result<std::size_t, TransportError> NoiseIkHandshake::read_response(
    std::span<const std::uint8_t> packet, std::span<std::uint8_t> payload_out) noexcept {
    if (!impl_) {
        return common::make_unexpected(TransportError::kInvalidState);
    }
    return impl_->read_message(PacketType::kHandshakeResponse, HandshakeEvent::kResponseRead,
                               kHandshakeResponseOverheadBytes, packet, payload_out);
}

common::Result<PublicKey, TransportError> NoiseIkHandshake::remote_static_public_key()
    const noexcept {
    if (!impl_ || !impl_->has_remote_public_key) {
        return common::make_unexpected(TransportError::kInvalidState);
    }
    return impl_->remote_public_key;
}

ConnectionId NoiseIkHandshake::connection_id() const noexcept {
    return impl_ ? impl_->connection_id : 0;
}

common::Result<SecureChannel, TransportError> NoiseIkHandshake::finish() noexcept {
    if (!impl_ || !impl_->accepts(HandshakeEvent::kFinished)) {
        return common::make_unexpected(TransportError::kInvalidState);
    }
    CipherStateHolder send_cipher;
    CipherStateHolder receive_cipher;
    if (noise_handshakestate_split(impl_->handshake, send_cipher.out(), receive_cipher.out()) !=
        NOISE_ERROR_NONE) {
        return impl_->fail();
    }
    TransportKey send_key{};
    TransportKey receive_key{};
    const bool derived = derive_transport_key(send_cipher.get(), send_key) &&
                         derive_transport_key(receive_cipher.get(), receive_key);
    std::optional<SecureChannel> channel;
    if (derived) {
        channel.emplace(SecureChannel{
            .sealer = PacketSealer(impl_->connection_id, send_key),
            .opener = PacketOpener(impl_->connection_id, receive_key),
        });
    }
    sodium_memzero(send_key.data(), send_key.size());
    sodium_memzero(receive_key.data(), receive_key.size());
    if (!channel) {
        return impl_->fail();
    }
    impl_->advance(HandshakeEvent::kFinished);
    // ハンドシェイクの状態はもう使わないので、鍵の材料を早く消すために解放する。
    impl_.reset();
    return std::move(*channel);
}

}  // namespace vocroz::transport
