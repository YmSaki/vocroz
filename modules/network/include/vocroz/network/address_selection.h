// SPDX-License-Identifier: AGPL-3.0-only
//
// ランデブーサーバーに申告するローカル候補アドレスと、ランデブーサーバーのアドレスの選び方。
// 選び方は docs/adr/0021-udp-socket-and-nat-traversal.md の「NAT 越えの手順」に従う。
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "vocroz/network/udp_address.h"

namespace vocroz::network {

/// ローカル候補アドレスを選び、優先する順に並べる。
/// 順序は、自動ポート開放の外部アドレス、IPv6 のグローバルアドレス、IPv4 のアドレス、
/// IPv6 のユニークローカルアドレス。直結できる見込みの高い候補を先に置き、仕様の試行順を保つ。
/// 未指定、ループバック、リンクローカル、マルチキャスト、Teredo のアドレスと重複は除き、
/// 最大 kMaxLocalCandidates 個にする。
/// @param interface_addresses NetworkEnvironment::interface_addresses() の結果（ポートは無視する）
/// @param local_port          UdpEndpoint::local_port()。ローカルのアドレスにはこのポートを付ける
/// @param mapped              自動ポート開放で得た外部アドレス。なければ std::nullopt
/// @return 候補アドレス
/// スレッド安全性: 純粋関数で、どのスレッドからも呼び出し可能。
[[nodiscard]] std::vector<UdpAddress> select_local_candidates(
    std::span<const UdpAddress> interface_addresses, std::uint16_t local_port,
    const std::optional<UdpAddress>& mapped);

/// 名前解決の結果から、ランデブーサーバーに使うアドレスを選ぶ。IPv4 を優先する。
/// ホールパンチはサーバーから見た外部アドレスを相手に渡すので、親機と子機が同じアドレスファミリーで
/// 登録しないと、相手が届かないアドレスを受け取る。IPv4 はほぼすべての回線で使えるため、IPv4
/// にそろえる。 IPv6 のグローバルアドレスはローカル候補として別に申告する。
/// @param resolved NetworkEnvironment::resolve() の結果
/// @return 選んだアドレス。resolved が空なら std::nullopt
/// スレッド安全性: 純粋関数で、どのスレッドからも呼び出し可能。
[[nodiscard]] std::optional<UdpAddress> select_server_address(
    std::span<const UdpAddress> resolved) noexcept;

/// 相手へ送っても届く見込みのあるアドレスなら true を返す。
/// 未指定、マルチキャスト、ポート 0 は false。ランデブーサーバーの検証と同じ規則で、
/// 受け取った候補で関係のない宛先へ送らされないようにするため。
/// @param address 調べるアドレス
/// スレッド安全性: 純粋関数で、どのスレッドからも呼び出し可能。
[[nodiscard]] bool is_routable_candidate(const UdpAddress& address) noexcept;

}  // namespace vocroz::network
