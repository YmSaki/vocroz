// SPDX-License-Identifier: AGPL-3.0-only
//
// Noise_IK_25519_ChaChaPoly_BLAKE2s による鍵交換（docs/adr/0001-key-exchange-noise-ik.md）。
// 開始側（子機）は、招待トークンで受け取った応答側（親機）の静的公開鍵を使って
// 1 つ目のメッセージを送る。応答側が 2 つ目のメッセージを返すと、
// 両側で接続ごとの送信鍵と受信鍵が決まる。
// 実装は noise-c と libsodium に任せ、暗号プリミティブは自作しない。
// 各メッセージには任意のペイロード（招待トークン、参加申請など）を暗号化して載せられる。
// 承認の判断はしない。応答側は remote_static_public_key() とペイロードを session に渡し、
// session が承認するかを決める。
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include "vocroz/common/result.h"
#include "vocroz/transport/constants.h"
#include "vocroz/transport/packet_header.h"
#include "vocroz/transport/secure_channel.h"
#include "vocroz/transport/static_key_pair.h"
#include "vocroz/transport/transport_error.h"

namespace vocroz::transport {

/// 1 つ目のハンドシェイクパケットの、ペイロード以外の長さ（バイト）。
/// 固定ヘッダー、開始側の一時公開鍵（32）、暗号化した開始側の静的公開鍵（32 + タグ 16）、
/// ペイロードの認証タグ（16）。
inline constexpr std::size_t kHandshakeInitiationOverheadBytes = kPacketHeaderBytes + 32 + 48 + 16;

/// 2 つ目のハンドシェイクパケットの、ペイロード以外の長さ（バイト）。
/// 固定ヘッダー、応答側の一時公開鍵（32）、ペイロードの認証タグ（16）。
inline constexpr std::size_t kHandshakeResponseOverheadBytes = kPacketHeaderBytes + 32 + 16;

/// 1 つ目のハンドシェイクパケットに載せられるペイロードの最大長（バイト）。
inline constexpr std::size_t kMaxHandshakeInitiationPayloadBytes =
    kMaxPacketBytes - kHandshakeInitiationOverheadBytes;

/// 2 つ目のハンドシェイクパケットに載せられるペイロードの最大長（バイト）。
inline constexpr std::size_t kMaxHandshakeResponsePayloadBytes =
    kMaxPacketBytes - kHandshakeResponseOverheadBytes;

/// 新しい接続 ID を乱数（libsodium の CSPRNG）で作る。開始側がハンドシェイクの前に呼ぶ。
/// 応答側で既存の接続と重なったときの扱いは session が決める。
/// @return 接続 ID。暗号ライブラリを初期化できなければ kCryptoUnavailable
/// スレッド安全性: どのスレッドからも呼び出し可能。
[[nodiscard]] common::Result<ConnectionId, TransportError> generate_connection_id() noexcept;

/// Noise_IK のハンドシェイク 1 回分。
/// 開始側は write_initiation() → read_response() → finish()、
/// 応答側は read_initiation() → write_response() → finish() の順に呼ぶ。
/// 順序が誤っていれば kInvalidState を返す。一度失敗したら以降の呼び出しはすべて失敗するので、
/// やり直すときは新しいオブジェクトを作る。
/// 1 つ目のメッセージのリプレイは、このクラスでは検出しない（応答しても攻撃者は鍵を得られない）。
/// 古い参加申請の再送を拒むのは、ペイロードを検証する session の責務とする。
/// スレッド安全性: 1 つのオブジェクトは 1 つのスレッドから使う
/// （コントロールスレッドかネットワーク受信スレッド）。
/// 生成時にヒープを確保するので、オーディオスレッドからは呼ばない。
class NoiseIkHandshake {
public:
    /// 開始側（子機）のハンドシェイクを作る。
    /// @param local_keys       開始側の静的鍵
    /// @param responder_public 応答側（親機）の静的公開鍵。招待トークンから得る
    /// @param connection_id    generate_connection_id() で作った接続 ID
    /// @return ハンドシェイク。暗号ライブラリを初期化できなければ kCryptoUnavailable
    [[nodiscard]] static common::Result<NoiseIkHandshake, TransportError> create_initiator(
        const StaticKeyPair& local_keys, const PublicKey& responder_public,
        ConnectionId connection_id) noexcept;

    /// 応答側（親機）のハンドシェイクを作る。接続 ID は 1 つ目のメッセージから読む。
    /// @param local_keys 応答側の静的鍵
    /// @return ハンドシェイク。暗号ライブラリを初期化できなければ kCryptoUnavailable
    [[nodiscard]] static common::Result<NoiseIkHandshake, TransportError> create_responder(
        const StaticKeyPair& local_keys) noexcept;

    NoiseIkHandshake(const NoiseIkHandshake&) = delete;
    NoiseIkHandshake& operator=(const NoiseIkHandshake&) = delete;
    /// ムーブ元は以降の呼び出しで kInvalidState を返す。
    NoiseIkHandshake(NoiseIkHandshake&& other) noexcept;
    /// ムーブ元は以降の呼び出しで kInvalidState を返す。
    NoiseIkHandshake& operator=(NoiseIkHandshake&& other) noexcept;
    /// 途中の鍵の材料は noise-c が破棄時にゼロで上書きする。
    ~NoiseIkHandshake();

    /// 開始側: 1 つ目のハンドシェイクパケットを作る。
    /// @param payload 暗号化して載せるペイロード（kMaxHandshakeInitiationPayloadBytes バイトまで）
    /// @param out     パケットの書き込み先。
    ///                kHandshakeInitiationOverheadBytes + payload.size() バイト以上
    /// @return 書き込んだバイト数。失敗の理由は kPayloadTooLarge、kBufferTooSmall、
    ///         kInvalidState、kHandshakeFailed
    [[nodiscard]] common::Result<std::size_t, TransportError> write_initiation(
        std::span<const std::uint8_t> payload, std::span<std::uint8_t> out) noexcept;

    /// 応答側: 1 つ目のハンドシェイクパケットを読む。成功すると開始側の静的公開鍵がわかる。
    /// @param packet      受信したパケット全体
    /// @param payload_out 復号したペイロードの書き込み先。packet.size() バイトあれば足りる
    /// @return 復号したペイロードのバイト数。ヘッダーの検証エラー、kUnexpectedPacketType、
    ///         kTruncated、kBufferTooSmall、kInvalidState、kHandshakeFailed（改ざん、宛先の鍵の不一致）
    [[nodiscard]] common::Result<std::size_t, TransportError> read_initiation(
        std::span<const std::uint8_t> packet, std::span<std::uint8_t> payload_out) noexcept;

    /// 応答側: 2 つ目のハンドシェイクパケットを作る。
    /// @param payload 暗号化して載せるペイロード（kMaxHandshakeResponsePayloadBytes バイトまで）
    /// @param out     パケットの書き込み先。
    ///                kHandshakeResponseOverheadBytes + payload.size() バイト以上
    /// @return 書き込んだバイト数。失敗の理由は kPayloadTooLarge、kBufferTooSmall、
    ///         kInvalidState、kHandshakeFailed
    [[nodiscard]] common::Result<std::size_t, TransportError> write_response(
        std::span<const std::uint8_t> payload, std::span<std::uint8_t> out) noexcept;

    /// 開始側: 2 つ目のハンドシェイクパケットを読む。
    /// @param packet      受信したパケット全体
    /// @param payload_out 復号したペイロードの書き込み先。packet.size() バイトあれば足りる
    /// @return 復号したペイロードのバイト数。ヘッダーの検証エラー、kUnexpectedPacketType、
    ///         kConnectionMismatch、kTruncated、kBufferTooSmall、kInvalidState、
    ///         kHandshakeFailed（改ざん、応答側が想定した親機でない）
    [[nodiscard]] common::Result<std::size_t, TransportError> read_response(
        std::span<const std::uint8_t> packet, std::span<std::uint8_t> payload_out) noexcept;

    /// 相手の静的公開鍵を返す。開始側では生成時に渡した値、応答側では read_initiation()
    /// の成功後の値。
    /// @return 公開鍵。応答側で read_initiation() の前なら kInvalidState
    [[nodiscard]] common::Result<PublicKey, TransportError> remote_static_public_key()
        const noexcept;

    /// 接続 ID を返す。応答側では read_initiation() の成功前は 0。
    [[nodiscard]] ConnectionId connection_id() const noexcept;

    /// ハンドシェイクを終え、送信鍵と受信鍵を持つ SecureChannel を作る。
    /// このオブジェクトは以降使えなくなる。
    /// @return 接続の送信と受信の組。ハンドシェイクが完了していなければ kInvalidState
    [[nodiscard]] common::Result<SecureChannel, TransportError> finish() noexcept;

private:
    struct Impl;

    explicit NoiseIkHandshake(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_;
};

}  // namespace vocroz::transport
