// SPDX-License-Identifier: AGPL-3.0-only
//
// network のアダプター（Winsock、Win32、miniupnpc）のファクトリー関数。
// 具象クラスは公開せず、コンポジションルートはここからポートの実装を受け取る（AGENTS.md 5.1）。
// どの関数も Windows のビルドでだけ定義する。
#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

#include "vocroz/common/result.h"
#include "vocroz/network/network_error.h"
#include "vocroz/network/ports/datagram_listener.h"
#include "vocroz/network/ports/https_transport.h"
#include "vocroz/network/ports/network_environment.h"
#include "vocroz/network/ports/port_mapper.h"
#include "vocroz/network/ports/udp_endpoint.h"
#include "vocroz/transport/ports/random_source.h"

namespace vocroz::network {

/// Winsock の UDP ソケットを開き、受信用のスレッド（ネットワーク受信スレッド）を起動する。
/// IPv4 と IPv6 の両方を 1 本で扱うデュアルスタックのソケットで、IPv6 を使えない環境では IPv4
/// だけで開く。 受信したデータグラムは、受信スレッドから listener
/// に渡す。破棄すると受信スレッドを止めて待つ。
/// @param port     バインドするポート。0 なら OS が空いているポートを選ぶ
/// @param listener 受信したデータグラムの渡し先。戻り値より長く生きること
/// @return ソケット。作れなければ kSocketUnavailable、バインドできなければ kBindFailed
/// スレッド安全性: コントロールスレッドから呼ぶ。
[[nodiscard]] common::Result<std::unique_ptr<UdpEndpoint>, NetworkError> make_winsock_udp_endpoint(
    std::uint16_t port, DatagramListener& listener);

/// Win32 の API（getaddrinfo、GetAdaptersAddresses）による NetworkEnvironment を作る。
/// スレッド安全性: どのスレッドからも呼び出し可能。
[[nodiscard]] std::unique_ptr<NetworkEnvironment> make_win_network_environment();

/// PCP、NAT-PMP、UPnP-IGD の順にポート開放を試す PortMapper を作る。
/// PCP と NAT-PMP は Winsock でデフォルトゲートウェイと通信し、UPnP-IGD は miniupnpc を使う。
/// @param random PCP の nonce に使う乱数。戻り値より長く生きること
/// スレッド安全性: どのスレッドからも呼び出し可能。戻り値は 1 つのスレッドから使う。
[[nodiscard]] std::unique_ptr<PortMapper> make_port_mapper(transport::RandomSource& random);

/// WinHTTP でランデブーサーバーの招待 API に POST する HttpsTransport を作る。
/// HttpsRendezvousClient に渡して使う。TLS の証明書は OS の証明書ストアで検証し、
/// プロキシは OS の設定に従う。リダイレクトには従わない。
/// @param base_url ランデブーサーバーの URL（例: "https://vocroz.example"）。形式は
/// parse_rendezvous_url()
/// @return トランスポート。URL が正しくなければ kInvalidServerUrl、WinHTTP を初期化できなければ
///         kHttpsConnectFailed。サーバーへの接続は最初の要求のときに行う
/// スレッド安全性: どのスレッドからも呼び出し可能。戻り値は 1 つのスレッドから使う。
[[nodiscard]] common::Result<std::unique_ptr<HttpsTransport>, NetworkError>
make_winhttp_https_transport(std::string_view base_url);

}  // namespace vocroz::network
