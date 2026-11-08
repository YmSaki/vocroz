// SPDX-License-Identifier: AGPL-3.0-only
//
// 子機と親機の間の UDP パケットの固定ヘッダー。
// ヘッダーは暗号化せず、暗号化済みパケットでは AEAD の関連データにして改ざんを検出する
// （docs/adr/0001-key-exchange-noise-ik.md、docs/adr/0014-transport-wire-format.md）。
// 受信側は復号の前にヘッダーだけを読み、接続 ID で届け先の接続を選ぶ。
// 送信元のアドレスではなく接続 ID で接続を識別するので、NAT のマッピングが変わったときや、
// 将来の不透明中継を経由したときも、同じ接続として扱える。
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "vocroz/common/result.h"
#include "vocroz/transport/transport_error.h"

namespace vocroz::transport {

/// 接続 ID。開始側（子機）がハンドシェイクの開始時に乱数で決め、両方向のすべてのパケットに載せる。
using ConnectionId = std::uint32_t;

/// パケットの種別。値はワイヤー上の 1 バイトにそのまま載るので、既存の値は変えない。
enum class PacketType : std::uint8_t {
    /// Noise_IK の 1 つ目のメッセージ（開始側 → 応答側）。
    kHandshakeInitiation = 1,
    /// Noise_IK の 2 つ目のメッセージ（応答側 → 開始側）。
    kHandshakeResponse = 2,
    /// 暗号化したメディア（Opus のフレーム）。
    kMedia = 3,
    /// 暗号化した制御メッセージ（承認、ミュート通知、キープアライブなど）。
    kControl = 4,
};

/// 固定ヘッダーの長さ（バイト）。
/// バージョン（1）、種別（1）、接続 ID（4）、シーケンス番号（8）をビッグエンディアンで並べる。
inline constexpr std::size_t kPacketHeaderBytes = 14;

/// 検証済みの固定ヘッダー。
struct PacketHeader {
    /// パケットの種別。
    PacketType type;
    /// 接続 ID。
    ConnectionId connection_id;
    /// 送信方向ごとのシーケンス番号。暗号化済みパケットでは AEAD の nonce を兼ねる。
    /// ハンドシェイクのパケットでは 0。
    std::uint64_t sequence;
};

/// 受信したパケットの固定ヘッダーを読み、検証する。復号はしない。
/// ヘッダーは認証前の値なので、接続を選ぶためだけに使い、状態の変更には使わない。
/// @param packet 受信したパケット全体
/// @return 検証済みのヘッダー。短すぎれば kTruncated、バージョンが違えば kUnsupportedVersion、
///         種別が未定義なら kUnknownPacketType
/// スレッド安全性: 純粋関数で、どのスレッドからも呼び出し可能。ヒープを確保しない。
[[nodiscard]] common::Result<PacketHeader, TransportError> parse_packet_header(
    std::span<const std::uint8_t> packet) noexcept;

}  // namespace vocroz::transport
