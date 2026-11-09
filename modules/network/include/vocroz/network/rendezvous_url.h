// SPDX-License-Identifier: AGPL-3.0-only
//
// ランデブーサーバーの URL（設定値）を、HTTPS の接続に使う部品に分ける。
// WinHTTP のアダプターの前で形式を検証し、http を誤って設定しても招待トークンを
// 平文でインターネットに流さないようにする。
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "vocroz/common/result.h"
#include "vocroz/network/network_error.h"

namespace vocroz::network {

/// 検証済みのランデブーサーバーの URL。
struct RendezvousUrl {
    /// TLS で接続するか。false は開発用のループバックのサーバーだけ。
    bool secure = true;
    /// ホスト名か IP アドレス。IPv6 のアドレスは角かっこを外した形（例: "::1"）。
    std::string host;
    /// ポート。URL で省略されていれば 443（http なら 80）。
    std::uint16_t port = 443;
    /// API のパスの前に付けるパス。末尾の / を除いた形で、なければ空（例: "/rendezvous"）。
    std::string base_path;

    friend bool operator==(const RendezvousUrl&, const RendezvousUrl&) = default;
};

/// ランデブーサーバーの URL を検証して分ける。
/// 受け付けるのは https:// の URL と、ホストが localhost、127.0.0.1、[::1] の http:// の URL
/// （手元でランデブーサーバーを動かす開発用）だけ。ユーザー情報（user@）、クエリ、フラグメントを
/// 含む URL と、パスに空白や ASCII 以外の文字を含む URL は受け付けない。ホストとパスは ASCII
/// だけになる。
/// @param url 設定されたランデブーサーバーの URL（例: "https://vocroz.example"）
/// @return 分けた URL。形式が正しくなければ kInvalidServerUrl
/// スレッド安全性: どのスレッドからも呼び出し可能（状態を持たない）。
[[nodiscard]] common::Result<RendezvousUrl, NetworkError> parse_rendezvous_url(
    std::string_view url);

}  // namespace vocroz::network
