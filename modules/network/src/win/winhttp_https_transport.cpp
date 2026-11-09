// SPDX-License-Identifier: AGPL-3.0-only
//
// WinHTTP による HttpsTransport の実装。

#include "win/winhttp_https_transport.h"

#include <chrono>
#include <optional>
#include <utility>

#include "network_constants.h"

namespace vocroz::network::win {
namespace {

/// User-Agent。サーバーのログで VOCROZ からの要求を見分けるため。バージョンは載せない
/// （古いクライアントを狙った攻撃の手がかりを渡さないため）。
constexpr const wchar_t* kUserAgent = L"VOCROZ";

/// 要求に付けるヘッダー。招待トークンを含むので、途中の機器にもキャッシュさせない。
constexpr const wchar_t* kRequestHeaders =
    L"Content-Type: application/json; charset=utf-8\r\nCache-Control: no-store\r\n";

/// OS のプロキシ設定（自動検出と PAC を含む）に従うアクセスの種類。Windows 8.1 以降の値で、
/// 古い SDK（mingw-w64 など）のヘッダーにはないので値で持つ。
#ifdef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
constexpr DWORD kAutomaticProxy = WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY;
#else
constexpr DWORD kAutomaticProxy = 4;
#endif

/// ASCII の文字列を UTF-16 にする。ホストとパスは parse_rendezvous_url() と招待 API の定数で
/// ASCII に限っているので、1 文字ずつの変換で足りる。
std::wstring widen_ascii(std::string_view text) {
    return {text.begin(), text.end()};
}

/// 直前の WinHTTP の失敗を原因別のエラーにする。
NetworkError last_error() {
    switch (::GetLastError()) {
        case ERROR_WINHTTP_NAME_NOT_RESOLVED:
            return NetworkError::kResolveFailed;
        case ERROR_WINHTTP_TIMEOUT:
            return NetworkError::kTimedOut;
        default:
            // 接続の拒否、TLS のハンドシェイクや証明書の検証の失敗、途中の切断など。
            return NetworkError::kHttpsConnectFailed;
    }
}

/// 応答の本文を上限まで読む。
/// @return 読めたら std::nullopt。上限を超えたら kMalformedResponse、読み取りの失敗はその原因
std::optional<NetworkError> read_body(HINTERNET request, std::string& body) {
    for (;;) {
        DWORD available = 0;
        if (!::WinHttpQueryDataAvailable(request, &available)) {
            return last_error();
        }
        if (available == 0) {
            return std::nullopt;
        }
        if (body.size() + available > kMaxHttpResponseBytes) {
            return NetworkError::kMalformedResponse;
        }
        const std::size_t offset = body.size();
        body.resize(offset + available);
        DWORD read = 0;
        if (!::WinHttpReadData(request, body.data() + offset, available, &read)) {
            return last_error();
        }
        body.resize(offset + read);
    }
}

}  // namespace

common::Result<std::unique_ptr<HttpsTransport>, NetworkError> WinHttpHttpsTransport::open(
    const RendezvousUrl& url) {
    WinHttpHandle session(::WinHttpOpen(kUserAgent, kAutomaticProxy, WINHTTP_NO_PROXY_NAME,
                                        WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) {
        // Windows 8.1 より前は自動のプロキシを指定できないので、既定のプロキシ設定で開き直す。
        session.reset(::WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    }
    if (!session) {
        return common::make_unexpected(NetworkError::kHttpsConnectFailed);
    }
    const int timeout_ms = static_cast<int>(kHttpsStepTimeout.count());
    if (!::WinHttpSetTimeouts(session.get(), timeout_ms, timeout_ms, timeout_ms, timeout_ms)) {
        return common::make_unexpected(NetworkError::kHttpsConnectFailed);
    }
    // 招待 API はリダイレクトを返さない。リダイレクトに従うと、招待トークンを含む本文を
    // 別のサーバーへ送り直しかねないので、従わずにそのまま応答として扱う。
    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    if (!::WinHttpSetOption(session.get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirect,
                            sizeof(redirect))) {
        return common::make_unexpected(NetworkError::kHttpsConnectFailed);
    }
    const std::wstring host = widen_ascii(url.host);
    WinHttpHandle connection(::WinHttpConnect(session.get(), host.c_str(), url.port, 0));
    if (!connection) {
        return common::make_unexpected(NetworkError::kHttpsConnectFailed);
    }
    return std::unique_ptr<HttpsTransport>(new WinHttpHttpsTransport(
        std::move(session), std::move(connection), url.secure, url.base_path));
}

WinHttpHttpsTransport::WinHttpHttpsTransport(WinHttpHandle session, WinHttpHandle connection,
                                             bool secure, std::string base_path)
    : session_(std::move(session)),
      connection_(std::move(connection)),
      secure_(secure),
      base_path_(std::move(base_path)) {}

common::Result<HttpResponse, NetworkError> WinHttpHttpsTransport::post_json(std::string_view path,
                                                                            std::string_view body) {
    const std::wstring object = widen_ascii(base_path_) + widen_ascii(path);
    const DWORD flags = WINHTTP_FLAG_REFRESH | (secure_ ? WINHTTP_FLAG_SECURE : 0);
    WinHttpHandle request(::WinHttpOpenRequest(connection_.get(), L"POST", object.c_str(), nullptr,
                                               WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                               flags));
    if (!request) {
        return common::make_unexpected(last_error());
    }
    // 本文は WinHTTP が送り終えるまで読むだけで書き換えないが、API の型が非 const のポインター。
    auto* const data = const_cast<char*>(body.data());
    const auto size = static_cast<DWORD>(body.size());
    if (!::WinHttpSendRequest(request.get(), kRequestHeaders, static_cast<DWORD>(-1L), data, size,
                              size, 0) ||
        !::WinHttpReceiveResponse(request.get(), nullptr)) {
        return common::make_unexpected(last_error());
    }
    DWORD status = 0;
    DWORD status_size = sizeof(status);
    if (!::WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                               WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
                               WINHTTP_NO_HEADER_INDEX)) {
        return common::make_unexpected(NetworkError::kMalformedResponse);
    }
    HttpResponse response;
    response.status = static_cast<int>(status);
    if (const auto error = read_body(request.get(), response.body)) {
        return common::make_unexpected(*error);
    }
    return response;
}

}  // namespace vocroz::network::win
