// SPDX-License-Identifier: AGPL-3.0-only
//
// network の中だけで使う定数。値の決定は docs/adr/0021-udp-socket-and-nat-traversal.md。
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace vocroz::network {

using namespace std::chrono_literals;

/// PCP と NAT-PMP の 1 回ごとの応答の待ち時間。RFC 6886 と同じく 250ms から倍にするが、
/// 回数は 3 回に抑え、ゲートウェイが対応していない環境でセッションの作成を 1.75
/// 秒より長く待たせない。
inline constexpr std::array<std::chrono::milliseconds, 3> kGatewayRetryTimeouts = {250ms, 500ms,
                                                                                   1000ms};

/// PCP と NAT-PMP のサーバーのポート（RFC 6887、RFC 6886）。
inline constexpr std::uint16_t kPcpServerPort = 5351;

/// 登録できるまで REGISTER を送り直す間隔。子機は接続できるまでこの間隔で送り続ける。
/// サーバーは子機の REGISTER のたびに PUNCH を送るので、これがホールパンチの再試行の間隔にもなる。
inline constexpr std::chrono::milliseconds kRegisterRetryInterval = 1s;

/// 親機が登録できた後に REGISTER を送り直す間隔。サーバーの登録の寿命（30 秒）より十分に短くし、
/// 家庭用ルーターの UDP のマッピングの寿命（30 秒程度のものがある）の間に 1 回は送るようにする。
inline constexpr std::chrono::milliseconds kHostRegisterRefreshInterval = 10s;

/// 最初の REGISTER から REGISTERED が届かないとき、ランデブーサーバーに届かないとみなすまでの時間。
inline constexpr std::chrono::milliseconds kRendezvousTimeout = 5s;

/// 親機が最後の REGISTERED から、登録が切れたとみなすまでの時間。サーバーの登録の寿命と同じ値で、
/// これを過ぎると新しい子機の PUNCH が親機に届かない。
inline constexpr std::chrono::milliseconds kHostRegistrationLostAfter = 30s;

/// ホールパンチで相手の候補へ送る間隔。NAT のマッピングが開くのを待つ間の遅れを小さくしつつ、
/// 候補が 9 個のときの送信量（毎秒 180 パケット）を抑える。
inline constexpr std::chrono::milliseconds kPunchInterval = 50ms;

/// 最後の PUNCH から、ホールパンチを送り続ける時間。子機は 1 秒ごとに REGISTER を送るので、
/// 接続できるまでの間は PUNCH が届き続け、この時間が延長される。
inline constexpr std::chrono::milliseconds kPunchWindow = 5s;

/// 子機が経路の確立を打ち切るまでの時間。ランデブーサーバーの応答（最大 5 秒）と、
/// ホールパンチ（数百ミリ秒で開くことが多い）に余裕を持たせた値。
inline constexpr std::chrono::milliseconds kClientConnectTimeout = 10s;

}  // namespace vocroz::network
