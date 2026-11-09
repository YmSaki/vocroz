// SPDX-License-Identifier: AGPL-3.0-only
//
// WinHTTP による HttpsTransport。TLS の検証、プロキシの自動検出、証明書ストアは OS に任せる。
// OS に組み込まれていて追加のライブラリが要らず、利用者の環境のプロキシ設定にも従えるため
// （docs/adr/0021-udp-socket-and-nat-traversal.md）。
#pragma once

#include <memory>
#include <string>
#include <string_view>

#include <windows.h>
#include <winhttp.h>

#include "vocroz/common/result.h"
#include "vocroz/network/ports/https_transport.h"
#include "vocroz/network/rendezvous_url.h"

namespace vocroz::network::win {

/// WinHTTP のハンドルを閉じる削除子。
struct WinHttpHandleCloser {
    void operator()(HINTERNET handle) const noexcept {
        ::WinHttpCloseHandle(handle);
    }
};

/// WinHTTP のハンドルの RAII ラッパー。
using WinHttpHandle = std::unique_ptr<void, WinHttpHandleCloser>;

/// ランデブーサーバーへの
/// POST。セッションと接続のハンドルを使い回し、要求ごとに要求のハンドルを開く。 スレッド安全性: 1
/// つのオブジェクトは 1 つのスレッドから使う。
class WinHttpHttpsTransport final : public HttpsTransport {
public:
    /// WinHTTP のセッションを開く。この時点では接続しない。
    /// @param url 検証済みのランデブーサーバーの URL
    /// @return 開いたトランスポート。WinHTTP を初期化できなければ kHttpsConnectFailed
    [[nodiscard]] static common::Result<std::unique_ptr<HttpsTransport>, NetworkError> open(
        const RendezvousUrl& url);

    [[nodiscard]] common::Result<HttpResponse, NetworkError> post_json(
        std::string_view path, std::string_view body) override;

private:
    WinHttpHttpsTransport(WinHttpHandle session, WinHttpHandle connection, bool secure,
                          std::string base_path);

    WinHttpHandle session_;
    WinHttpHandle connection_;
    bool secure_;
    std::string base_path_;
};

}  // namespace vocroz::network::win
