// SPDX-License-Identifier: AGPL-3.0-only
//
// 親機の Noise の静的鍵を、保存した秘密鍵から読み込むか、なければ作って保存する
// （docs/adr/0027-persistence.md）。再起動しても同じ鍵を使い続けるのは、招待トークンに
// 入れた公開鍵と再接続で、子機が同じ親機だと確かめるため。
#pragma once

#include "vocroz/common/ports/logger.h"
#include "vocroz/common/result.h"
#include "vocroz/session/ports/secret_store.h"
#include "vocroz/transport/static_key_pair.h"
#include "vocroz/transport/transport_error.h"

namespace vocroz::engine {

/// 親機の静的鍵を用意する。
/// - 保存した鍵があれば、それを使う
/// - ない（初回）か壊れている（kCorrupted、長さの不正）なら、作り直して保存する
/// - 読み込みが I/O の失敗なら、保存した鍵を上書きしないよう、この起動だけの鍵を作って保存しない
/// - 保存に失敗しても、作った鍵でこの起動は続ける（次の起動では別の鍵になる）
/// 読み込んだ平文は使い終えたらゼロで上書きする。鍵の値はログに出さない。
/// @param secrets 秘密情報の保存先
/// @param logger  鍵を作り直したときなどの記録先
/// @return 鍵ペア。暗号ライブラリを初期化できなければ kCryptoUnavailable
/// スレッド安全性: コントロールスレッドから起動時に 1 回呼ぶ。
[[nodiscard]] common::Result<transport::StaticKeyPair, transport::TransportError> load_host_keys(
    session::SecretStore& secrets, common::Logger& logger);

}  // namespace vocroz::engine
