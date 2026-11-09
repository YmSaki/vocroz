// SPDX-License-Identifier: AGPL-3.0-only
//
// ランデブーサーバーのベース URL（例: https://rendezvous.example）から、UDP のランデブーに使う
// ホスト名を取り出す。招待の HTTPS と UDP のランデブーは同じサーバーに置く
// （docs/adr/0012-rendezvous-protocol.md）ので、設定は URL 1 つにまとめ、ホスト名はここで導出する。
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace vocroz::client {

/// ベース URL のホスト名を返す。
/// 受け付けるのは "https://" か "http://" で始まる URL で、ポート（:8443）とパスは無視する。
/// IPv6 のリテラル（[::1]）は角かっこを外して返す。
/// @param base_url ランデブーサーバーのベース URL
/// @return ホスト名。形式が正しくなければ std::nullopt
/// スレッド安全性: 純粋関数で、どのスレッドからも呼び出し可能。
[[nodiscard]] std::optional<std::string> rendezvous_host_from_url(std::string_view base_url);

}  // namespace vocroz::client
