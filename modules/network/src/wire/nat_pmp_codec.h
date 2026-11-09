// SPDX-License-Identifier: AGPL-3.0-only
//
// NAT-PMP（RFC 6886）の要求のシリアライズと応答のパース。
// PCP に対応していないゲートウェイで、外部アドレスの取得と UDP のポート転送に使う。
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>

#include "vocroz/common/result.h"
#include "vocroz/network/network_error.h"
#include "vocroz/network/udp_address.h"

namespace vocroz::network::wire {

/// 外部アドレスの要求の長さ（バイト）。
inline constexpr std::size_t kNatPmpExternalAddressRequestBytes = 2;

/// UDP の転送の要求の長さ（バイト）。
inline constexpr std::size_t kNatPmpMapRequestBytes = 12;

/// NAT-PMP の結果コード（RFC 6886 の Result Code）。使う値だけを名前にする。
enum class NatPmpResult : std::uint16_t {
    kSuccess = 0,
    kUnsupportedVersion = 1,
    kNotAuthorized = 2,
};

/// 外部アドレスの応答。
struct NatPmpExternalAddressResponse {
    std::uint16_t result = 0;
    /// 外部の IPv4 アドレス（ポートは 0）。
    UdpAddress external;
};

/// UDP の転送の応答。
struct NatPmpMapResponse {
    std::uint16_t result = 0;
    std::uint16_t internal_port = 0;
    std::uint16_t external_port = 0;
    std::chrono::seconds lifetime{0};
};

/// 外部アドレスの要求をシリアライズする。
/// @param out 書き込み先。kNatPmpExternalAddressRequestBytes バイト以上
/// @return 書き込んだバイト数。out が小さければ kSendFailed
/// スレッド安全性: 純粋関数で、どのスレッドからも呼び出し可能。
[[nodiscard]] common::Result<std::size_t, NetworkError> encode_nat_pmp_external_address_request(
    std::span<std::uint8_t> out) noexcept;

/// UDP の転送の要求をシリアライズする。
/// @param internal_port           転送先のローカルのポート
/// @param suggested_external_port 希望する外部ポート
/// @param lifetime                希望する寿命。0 なら削除
/// @param out                     書き込み先。kNatPmpMapRequestBytes バイト以上
/// @return 書き込んだバイト数。out が小さければ kSendFailed
/// スレッド安全性: 純粋関数で、どのスレッドからも呼び出し可能。
[[nodiscard]] common::Result<std::size_t, NetworkError> encode_nat_pmp_map_request(
    std::uint16_t internal_port, std::uint16_t suggested_external_port,
    std::chrono::seconds lifetime, std::span<std::uint8_t> out) noexcept;

/// 外部アドレスの応答をパースする。結果コードが成功以外でも、形式が正しければ値を返す。
/// @param datagram 受信したデータグラム
/// @return 応答。バージョン、オペコード、長さのどれかが不正なら kMalformedResponse
/// スレッド安全性: 純粋関数で、どのスレッドからも呼び出し可能。
[[nodiscard]] common::Result<NatPmpExternalAddressResponse, NetworkError>
parse_nat_pmp_external_address_response(std::span<const std::uint8_t> datagram) noexcept;

/// UDP の転送の応答をパースする。結果コードが成功以外でも、形式が正しければ値を返す。
/// @param datagram 受信したデータグラム
/// @return 応答。バージョン、オペコード、長さのどれかが不正なら kMalformedResponse
/// スレッド安全性: 純粋関数で、どのスレッドからも呼び出し可能。
[[nodiscard]] common::Result<NatPmpMapResponse, NetworkError> parse_nat_pmp_map_response(
    std::span<const std::uint8_t> datagram) noexcept;

}  // namespace vocroz::network::wire
