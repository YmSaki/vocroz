// SPDX-License-Identifier: AGPL-3.0-only
//
// 結合テストで、子機と親機の暗号化済みの接続を用意する。
// ハンドシェイクを経路に流すのは session の役割で、まだ実装がないため、ここではプロセス内で
// Noise_IK を最後まで実行し、確立した SecureChannel の組だけを返す。
// session（S1）が入ったら、ハンドシェイクもシミュレーションのネットワーク越しに行う結合テストを足す。
#pragma once

#include "vocroz/common/result.h"
#include "vocroz/transport/secure_channel.h"
#include "vocroz/transport/transport_error.h"

namespace vocroz::test_harness {

/// 子機と親機の SecureChannel の組。
struct SecureLink {
    /// 子機（ハンドシェイクの開始側）。
    transport::SecureChannel client;
    /// 親機（ハンドシェイクの応答側）。
    transport::SecureChannel host;
};

/// 新しい鍵の組で Noise_IK のハンドシェイクを行い、確立した接続を返す。
/// @param connection_id 接続 ID
/// @return 確立した接続。鍵の生成かハンドシェイクに失敗したら、その TransportError
/// スレッド安全性: どのスレッドからも呼び出し可能。
[[nodiscard]] common::Result<SecureLink, transport::TransportError> establish_secure_link(
    transport::ConnectionId connection_id);

}  // namespace vocroz::test_harness
