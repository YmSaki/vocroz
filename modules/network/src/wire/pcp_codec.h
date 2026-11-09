// SPDX-License-Identifier: AGPL-3.0-only
//
// PCP（Port Control Protocol、RFC 6887）の MAP 要求のシリアライズと、応答のパース。
// UDP のポート転送の作成、更新、削除（寿命
// 0）にだけ使う。オプションは送らず、受け取っても無視する。
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>

#include "vocroz/common/result.h"
#include "vocroz/network/network_error.h"
#include "vocroz/network/udp_address.h"

namespace vocroz::network::wire {

/// PCP の MAP 要求と応答の長さ（バイト）。共通ヘッダー 24 バイトと MAP の本体 36 バイト。
inline constexpr std::size_t kPcpMapMessageBytes = 60;

/// PCP の応答の最大長（バイト）。RFC 6887 の上限。
inline constexpr std::size_t kPcpMaxMessageBytes = 1100;

/// MAP の要求と応答を対応づける乱数。
using PcpNonce = std::array<std::uint8_t, 12>;

/// PCP の結果コード（RFC 6887 の Result Code）。使う値だけを名前にする。
enum class PcpResult : std::uint8_t {
    kSuccess = 0,
    kUnsupportedVersion = 1,
    kNotAuthorized = 2,
};

/// MAP 要求。
struct PcpMapRequest {
    PcpNonce nonce{};
    /// ゲートウェイへ届くときのこちらの IP アドレス（ポートは使わない）。
    UdpAddress client;
    std::uint16_t internal_port = 0;
    /// 希望する外部ポート。同じ番号を希望すると、ルーターの再起動後も同じ外部ポートになりやすい。
    std::uint16_t suggested_external_port = 0;
    /// 希望する寿命。0 なら削除。
    std::chrono::seconds lifetime{0};
};

/// MAP 応答。
struct PcpMapResponse {
    /// 結果コード。PcpResult にない値もそのまま入る。
    std::uint8_t result = 0;
    PcpNonce nonce{};
    std::uint16_t internal_port = 0;
    /// 割り当てられた外部アドレスとポート。
    UdpAddress external;
    std::chrono::seconds lifetime{0};
};

/// MAP 要求をシリアライズする。
/// @param request 送る要求
/// @param out     書き込み先。kPcpMapMessageBytes バイト以上
/// @return 書き込んだバイト数。out が小さければ kSendFailed
/// スレッド安全性: 純粋関数で、どのスレッドからも呼び出し可能。
[[nodiscard]] common::Result<std::size_t, NetworkError> encode_pcp_map_request(
    const PcpMapRequest& request, std::span<std::uint8_t> out) noexcept;

/// MAP 応答をパースする。結果コードが成功以外でも、形式が正しければ値を返す。
/// @param datagram 受信したデータグラム
/// @return 応答。バージョン、R ビット、オペコード、長さ、プロトコル番号のどれかが不正なら
/// kMalformedResponse スレッド安全性: 純粋関数で、どのスレッドからも呼び出し可能。
[[nodiscard]] common::Result<PcpMapResponse, NetworkError> parse_pcp_map_response(
    std::span<const std::uint8_t> datagram) noexcept;

/// 応答が NAT-PMP（バージョン 0）の形をしていれば true を返す。
/// PCP に対応していないゲートウェイは、PCP の要求に NAT-PMP のバージョンで応答する（RFC 6887
/// の付録）。
/// @param datagram 受信したデータグラム
/// スレッド安全性: 純粋関数で、どのスレッドからも呼び出し可能。
[[nodiscard]] bool is_nat_pmp_response(std::span<const std::uint8_t> datagram) noexcept;

}  // namespace vocroz::network::wire
