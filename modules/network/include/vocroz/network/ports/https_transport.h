// SPDX-License-Identifier: AGPL-3.0-only
//
// ランデブーサーバーの招待 API へ JSON を POST するポート。network の src/win/ が WinHTTP
// で実装する。 招待の登録、解決、失効の手順（HttpsRendezvousClient）を、OS の HTTP
// の実装から切り離して Linux の CI で単体テストできるようにするため。
#pragma once

#include <string>
#include <string_view>

#include "vocroz/common/result.h"
#include "vocroz/network/network_error.h"

namespace vocroz::network {

/// HTTP の応答。状態コードと本文だけを使う（招待 API はヘッダーで情報を返さない）。
struct HttpResponse {
    /// HTTP の状態コード（200、404 など）。
    int status = 0;
    /// 応答の本文。実装が上限（kMaxHttpResponseBytes）を超える本文を受け取ったら失敗にする。
    std::string body;
};

/// ランデブーサーバーへの HTTPS の POST。
/// スレッド安全性: 1 つのオブジェクトは 1 つのスレッドから使う。呼び出しはブロックする。
class HttpsTransport {
public:
    virtual ~HttpsTransport() = default;

    /// ランデブーサーバーの path へ、本文 body（application/json）を POST する。
    /// @param path サーバーの URL からの相対パス（例: "/v1/invites/resolve"）
    /// @param body JSON の本文。招待トークンなどを含むので、実装はログに出さない
    /// @return 応答（4xx と 5xx も応答として返す）。名前を解決できなければ kResolveFailed、
    ///         時間内に応答がなければ kTimedOut、接続や TLS が失敗したら kHttpsConnectFailed、
    ///         本文が大きすぎれば kMalformedResponse
    [[nodiscard]] virtual common::Result<HttpResponse, NetworkError> post_json(
        std::string_view path, std::string_view body) = 0;
};

}  // namespace vocroz::network
