// SPDX-License-Identifier: AGPL-3.0-only
//
// WinHTTP の HttpsTransport のテスト。ループバックで 1 回だけ応答する HTTP サーバーを立て、
// 要求の行、ヘッダー、本文の送り方と、応答の状態コードと本文の受け取り方を確かめる。
// TLS はループバックに証明書を用意できないので、開発用に許している http:// で確かめる。
// Windows でだけビルドする。

#include <cstdint>
#include <string>
#include <thread>
#include <utility>

#include <gtest/gtest.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include "vocroz/network/factories.h"

namespace vocroz::network {
namespace {

/// テストのための Winsock の初期化。
class WinsockSession {
public:
    WinsockSession() {
        WSADATA data{};
        ok_ = ::WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }
    ~WinsockSession() {
        if (ok_) {
            ::WSACleanup();
        }
    }
    WinsockSession(const WinsockSession&) = delete;
    WinsockSession& operator=(const WinsockSession&) = delete;

    [[nodiscard]] bool ok() const {
        return ok_;
    }

private:
    bool ok_ = false;
};

/// 127.0.0.1 の空いているポートで待ち受け、1 つの要求を読んで決めた応答を返すサーバー。
class OneShotHttpServer {
public:
    explicit OneShotHttpServer(std::string response) : response_(std::move(response)) {
        listener_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        ::bind(listener_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
        ::listen(listener_, 1);
        int len = sizeof(addr);
        ::getsockname(listener_, reinterpret_cast<sockaddr*>(&addr), &len);
        port_ = ntohs(addr.sin_port);
        thread_ = std::thread([this] { serve(); });
    }

    ~OneShotHttpServer() {
        // 要求が来なかったときも accept の待ちを解く。
        ::closesocket(listener_);
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    OneShotHttpServer(const OneShotHttpServer&) = delete;
    OneShotHttpServer& operator=(const OneShotHttpServer&) = delete;

    [[nodiscard]] std::uint16_t port() const {
        return port_;
    }

    /// 応答を返し終えるまで待ち、受け取った要求（ヘッダーと本文）を返す。
    [[nodiscard]] std::string take_request() {
        thread_.join();
        return request_;
    }

private:
    void serve() {
        const SOCKET client = ::accept(listener_, nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            return;
        }
        char buffer[4096];
        for (;;) {
            const auto header_end = request_.find("\r\n\r\n");
            if (header_end != std::string::npos &&
                request_.size() >= header_end + 4 + body_length()) {
                break;
            }
            const int n = ::recv(client, buffer, sizeof(buffer), 0);
            if (n <= 0) {
                break;
            }
            request_.append(buffer, static_cast<std::size_t>(n));
        }
        ::send(client, response_.data(), static_cast<int>(response_.size()), 0);
        ::shutdown(client, SD_BOTH);
        ::closesocket(client);
    }

    /// これまでに読んだヘッダーの Content-Length。まだ読めていなければ 0。
    [[nodiscard]] std::size_t body_length() const {
        const auto pos = request_.find("Content-Length: ");
        if (pos == std::string::npos) {
            return 0;
        }
        return std::stoul(request_.substr(pos + 16));
    }

    std::string response_;
    SOCKET listener_ = INVALID_SOCKET;
    std::uint16_t port_ = 0;
    std::thread thread_;
    std::string request_;
};

std::string http_response(int status, const std::string& reason, const std::string& body) {
    return "HTTP/1.1 " + std::to_string(status) + " " + reason +
           "\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) +
           "\r\nConnection: close\r\n\r\n" + body;
}

std::string loopback_url(std::uint16_t port, const std::string& path = "") {
    return "http://127.0.0.1:" + std::to_string(port) + path;
}

TEST(WinHttpHttpsTransportTest, PostsJsonUnderTheBasePathAndReadsTheResponse) {
    WinsockSession winsock;
    ASSERT_TRUE(winsock.ok());
    OneShotHttpServer server(http_response(410, "Gone", R"({"error":"INVITE_REVOKED"})"));
    auto transport = make_winhttp_https_transport(loopback_url(server.port(), "/rdv/"));
    ASSERT_TRUE(transport.has_value());

    const auto response =
        transport.value()->post_json("/v1/invites/resolve", R"({"code":"ABCDE12345"})");
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response.value().status, 410);
    EXPECT_EQ(response.value().body, R"({"error":"INVITE_REVOKED"})");

    const std::string request = server.take_request();
    EXPECT_EQ(request.rfind("POST /rdv/v1/invites/resolve HTTP/1.1\r\n", 0), 0U) << request;
    EXPECT_NE(request.find("Content-Type: application/json"), std::string::npos) << request;
    EXPECT_NE(request.find("Cache-Control: no-store"), std::string::npos) << request;
    EXPECT_NE(request.find("\r\n\r\n{\"code\":\"ABCDE12345\"}"), std::string::npos) << request;
}

TEST(WinHttpHttpsTransportTest, DoesNotFollowRedirects) {
    WinsockSession winsock;
    ASSERT_TRUE(winsock.ok());
    // 本文を別のサーバーへ送り直さないよう、リダイレクトはそのまま応答として返す。
    OneShotHttpServer server(
        "HTTP/1.1 307 Temporary Redirect\r\nLocation: http://127.0.0.1:1/x\r\n"
        "Content-Length: 0\r\nConnection: close\r\n\r\n");
    auto transport = make_winhttp_https_transport(loopback_url(server.port()));
    ASSERT_TRUE(transport.has_value());
    const auto response = transport.value()->post_json("/v1/invites", "{}");
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response.value().status, 307);
}

TEST(WinHttpHttpsTransportTest, RejectsAResponseBodyOverTheLimit) {
    WinsockSession winsock;
    ASSERT_TRUE(winsock.ok());
    OneShotHttpServer server(http_response(200, "OK", std::string(64 * 1024, 'x')));
    auto transport = make_winhttp_https_transport(loopback_url(server.port()));
    ASSERT_TRUE(transport.has_value());
    const auto response = transport.value()->post_json("/v1/invites/resolve", "{}");
    ASSERT_FALSE(response.has_value());
    EXPECT_EQ(response.error(), NetworkError::kMalformedResponse);
}

TEST(WinHttpHttpsTransportTest, ReportsARefusedConnection) {
    WinsockSession winsock;
    ASSERT_TRUE(winsock.ok());
    std::uint16_t closed_port = 0;
    {
        // 空いているポートを 1 つ取って閉じ、誰も待ち受けていないポートにする。
        OneShotHttpServer server("");
        closed_port = server.port();
    }
    auto transport = make_winhttp_https_transport(loopback_url(closed_port));
    ASSERT_TRUE(transport.has_value());
    const auto response = transport.value()->post_json("/v1/invites", "{}");
    ASSERT_FALSE(response.has_value());
    EXPECT_EQ(response.error(), NetworkError::kHttpsConnectFailed);
}

TEST(WinHttpHttpsTransportTest, RefusesPlainHttpToTheInternet) {
    const auto transport = make_winhttp_https_transport("http://vocroz.example");
    ASSERT_FALSE(transport.has_value());
    EXPECT_EQ(transport.error(), NetworkError::kInvalidServerUrl);
}

}  // namespace
}  // namespace vocroz::network
