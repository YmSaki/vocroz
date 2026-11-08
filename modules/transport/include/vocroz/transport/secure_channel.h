// SPDX-License-Identifier: AGPL-3.0-only
//
// ハンドシェイク後の暗号化済みパケットの送受信。
// 固定ヘッダーを AEAD（ChaCha20-Poly1305、libsodium）の関連データにし、
// シーケンス番号を nonce にして平文を暗号化する。受信側はシーケンス番号の窓でリプレイを排除する
// （docs/adr/0014-transport-wire-format.md）。
// 送信と受信を別のクラスにしているのは、送信スレッドと受信スレッドが別々に持てるようにするため。
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "vocroz/common/result.h"
#include "vocroz/transport/constants.h"
#include "vocroz/transport/media_payload.h"
#include "vocroz/transport/packet_header.h"
#include "vocroz/transport/transport_error.h"

namespace vocroz::transport {

class NoiseIkHandshake;

/// 送信方向または受信方向の AEAD の鍵の長さ（バイト）。
inline constexpr std::size_t kTransportKeyBytes = 32;

/// AEAD の認証タグの長さ（バイト）。
inline constexpr std::size_t kAeadTagBytes = 16;

/// 暗号化済みパケット 1 つに載せられる平文の最大長（バイト）。
inline constexpr std::size_t kMaxPlaintextBytes =
    kMaxPacketBytes - kPacketHeaderBytes - kAeadTagBytes;

/// 平文 plaintext_bytes を暗号化したパケットの長さ（バイト）を返す。
/// @param plaintext_bytes 平文の長さ
[[nodiscard]] constexpr std::size_t sealed_packet_bytes(std::size_t plaintext_bytes) noexcept {
    return kPacketHeaderBytes + plaintext_bytes + kAeadTagBytes;
}

/// 送信方向の暗号化。シーケンス番号を 0 から 1 ずつ進め、同じ nonce を二度使わない。
/// コピーできず、ムーブだけできる（同じ鍵とシーケンス番号を 2 か所で使うと nonce が重複するため）。
/// スレッド安全性: 1 つのオブジェクトは 1 つのスレッドから使う。ヒープを確保せず、ブロックしない。
class PacketSealer {
public:
    PacketSealer(const PacketSealer&) = delete;
    PacketSealer& operator=(const PacketSealer&) = delete;
    /// ムーブ元の鍵はゼロで上書きし、以降の送信は kInvalidState で失敗させる。
    PacketSealer(PacketSealer&& other) noexcept;
    /// ムーブ元の鍵はゼロで上書きし、以降の送信は kInvalidState で失敗させる。
    PacketSealer& operator=(PacketSealer&& other) noexcept;
    /// 鍵をゼロで上書きしてから破棄する。
    ~PacketSealer();

    /// メディアパケットを作る。
    /// @param payload 送るメディアの中身
    /// @param out     パケットの書き込み先。sealed_packet_bytes(kMediaPayloadHeaderBytes +
    ///                payload.opus_payload.size()) バイト以上
    /// @return 書き込んだバイト数。失敗の理由は次のとおりで、失敗時はシーケンス番号を進めない。
    ///         Opus のペイロードが空なら kMalformedPayload、
    ///         平文が kMaxPlaintextBytes を超えれば kPayloadTooLarge、
    ///         out が小さければ kBufferTooSmall、シーケンス番号を使い切れば kNonceExhausted
    /// スレッド安全性: 送信側のネットワークスレッドから呼ぶ（ヒープ確保、ロック、I/O なし）。
    [[nodiscard]] common::Result<std::size_t, TransportError> seal_media(
        const MediaPayload& payload, std::span<std::uint8_t> out) noexcept;

    /// 制御パケットを作る。制御メッセージの形式は信頼制御層が決め、ここでは不透明なバイト列として扱う。
    /// @param plaintext 制御メッセージ（空でもよい。キープアライブに使える）
    /// @param out       パケットの書き込み先。sealed_packet_bytes(plaintext.size()) バイト以上
    /// @return 書き込んだバイト数。失敗の理由は seal_media() と同じ（kMalformedPayload を除く）
    /// スレッド安全性: 送信側のネットワークスレッドから呼ぶ（ヒープ確保、ロック、I/O なし）。
    [[nodiscard]] common::Result<std::size_t, TransportError> seal_control(
        std::span<const std::uint8_t> plaintext, std::span<std::uint8_t> out) noexcept;

    /// 接続 ID を返す。
    [[nodiscard]] ConnectionId connection_id() const noexcept {
        return connection_id_;
    }

private:
    friend class NoiseIkHandshake;

    PacketSealer(ConnectionId connection_id,
                 const std::array<std::uint8_t, kTransportKeyBytes>& key) noexcept;

    // plaintext_bytes バイトの平文がすでに out のヘッダーの後ろに書かれている前提で、
    // ヘッダーを書き、平文をその場で暗号化する。
    [[nodiscard]] common::Result<std::size_t, TransportError> seal_in_place(
        PacketType type, std::size_t plaintext_bytes, std::span<std::uint8_t> out) noexcept;

    // 送信の前提条件（鍵がある、nonce が残っている、長さが収まる）を確かめる。
    [[nodiscard]] common::Result<void, TransportError> check_can_seal(
        std::size_t plaintext_bytes, std::size_t out_bytes) const noexcept;

    ConnectionId connection_id_ = 0;
    std::array<std::uint8_t, kTransportKeyBytes> key_{};
    std::uint64_t next_sequence_ = 0;
    bool has_key_ = false;
};

/// 受信して復号したパケット。plaintext は open() に渡した出力バッファの一部を指す。
struct OpenedPacket {
    /// kMedia か kControl。
    PacketType type;
    /// 認証済みのシーケンス番号。
    std::uint64_t sequence;
    /// 復号した平文。kMedia なら decode_media_payload() で MediaPayload に変換する。
    std::span<const std::uint8_t> plaintext;
};

/// 受信方向の復号とリプレイ排除。
/// 認証に成功したパケットのシーケンス番号だけを窓に記録するので、偽造パケットで窓を進めて
/// 正規のパケットを捨てさせることはできない。窓より古いパケットと受信済みのパケットは捨てる。
/// スレッド安全性: 1 つのオブジェクトは 1 つのスレッド（ネットワーク受信スレッド）から使う。
/// ヒープを確保せず、ブロックしない。
class PacketOpener {
public:
    /// リプレイ排除の窓の幅（パケット数）。最新の受信より、この数以上古いシーケンス番号は捨てる。
    /// 5ms フレームで毎秒 200 パケット（docs/adr/0002-opus-low-delay.md）のとき約 5 秒分で、
    /// 経路上の順序の入れ替わりより十分に長い。
    static constexpr std::size_t kReplayWindowPackets = 1024;

    PacketOpener(const PacketOpener&) = delete;
    PacketOpener& operator=(const PacketOpener&) = delete;
    /// ムーブ元の鍵はゼロで上書きし、以降の受信は kInvalidState で失敗させる。
    PacketOpener(PacketOpener&& other) noexcept;
    /// ムーブ元の鍵はゼロで上書きし、以降の受信は kInvalidState で失敗させる。
    PacketOpener& operator=(PacketOpener&& other) noexcept;
    /// 鍵をゼロで上書きしてから破棄する。
    ~PacketOpener();

    /// 受信したパケットを検証して復号する。失敗したパケットは状態を変えずに捨てる。
    /// @param packet        受信したパケット全体
    /// @param plaintext_out 平文の書き込み先。packet.size() - kPacketHeaderBytes - kAeadTagBytes
    ///                      バイト以上
    /// @return 復号したパケット。失敗の理由は kTruncated、kUnsupportedVersion、kUnknownPacketType、
    ///         kUnexpectedPacketType（ハンドシェイクのパケット）、kConnectionMismatch、kReplayed、
    ///         kAuthenticationFailed、kBufferTooSmall、kInvalidState（ムーブ元）
    /// スレッド安全性: ネットワーク受信スレッドから呼ぶ（ヒープ確保、ロック、I/O なし）。
    [[nodiscard]] common::Result<OpenedPacket, TransportError> open(
        std::span<const std::uint8_t> packet, std::span<std::uint8_t> plaintext_out) noexcept;

    /// 接続 ID を返す。
    [[nodiscard]] ConnectionId connection_id() const noexcept {
        return connection_id_;
    }

private:
    friend class NoiseIkHandshake;

    // 窓のビット列を 64 ビットの語で持つ。
    static constexpr std::size_t kWindowWords = kReplayWindowPackets / 64;
    static_assert(kReplayWindowPackets % 64 == 0, "窓の幅は 64 の倍数にする");

    PacketOpener(ConnectionId connection_id,
                 const std::array<std::uint8_t, kTransportKeyBytes>& key) noexcept;

    // sequence が未受信で、窓の範囲内かを返す。状態は変えない。
    [[nodiscard]] bool is_acceptable(std::uint64_t sequence) const noexcept;
    // 認証に成功した sequence を受信済みとして窓に記録する。
    void mark_received(std::uint64_t sequence) noexcept;

    ConnectionId connection_id_ = 0;
    std::array<std::uint8_t, kTransportKeyBytes> key_{};
    // これまでに認証に成功した最大のシーケンス番号。has_received_ が false の間は無効。
    std::uint64_t highest_sequence_ = 0;
    // ビット i は highest_sequence_ - i を受信済みかを表す。
    std::array<std::uint64_t, kWindowWords> window_{};
    bool has_received_ = false;
    bool has_key_ = false;
};

/// ハンドシェイクで確立した、1 つの接続の送信と受信の組。
struct SecureChannel {
    /// 送信方向。
    PacketSealer sealer;
    /// 受信方向。
    PacketOpener opener;
};

}  // namespace vocroz::transport
