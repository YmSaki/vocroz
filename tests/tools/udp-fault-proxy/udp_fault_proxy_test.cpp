// SPDX-License-Identifier: AGPL-3.0-only
//
// UdpFaultProxy の単体テスト（ループバックの実ソケット）。
// 子機から上流への中継、上流から子機への応答の中継、方向ごとの障害の適用を確かめる。

#include "udp_fault_proxy.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>

#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <sys/socket.h>
#include <unistd.h>

namespace vocroz::tools {
namespace {

using std::chrono::milliseconds;
using Clock = std::chrono::steady_clock;

// テスト用の非ブロッキングの UDP ソケット（ループバックの空きポート）。
class TestSocket {
public:
    TestSocket() {
        fd_ = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
        sockaddr_in address = loopback(0);
        EXPECT_EQ(::bind(fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)), 0);
    }
    TestSocket(const TestSocket&) = delete;
    TestSocket& operator=(const TestSocket&) = delete;
    ~TestSocket() {
        ::close(fd_);
    }

    static sockaddr_in loopback(std::uint16_t port) {
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        return address;
    }

    [[nodiscard]] sockaddr_in address() const {
        sockaddr_in address{};
        socklen_t length = sizeof(address);
        ::getsockname(fd_, reinterpret_cast<sockaddr*>(&address), &length);
        return address;
    }

    void send_to(const sockaddr_in& to, std::uint8_t tag) const {
        const std::uint8_t data[] = {tag};
        ::sendto(fd_, data, sizeof(data), 0, reinterpret_cast<const sockaddr*>(&to), sizeof(to));
    }

    // 1 つ受信する。届いていなければ nullopt。from に送信元を書く。
    std::optional<std::uint8_t> receive(sockaddr_in* from = nullptr) const {
        std::array<std::uint8_t, 16> buffer{};
        sockaddr_in source{};
        socklen_t length = sizeof(source);
        const auto size = ::recvfrom(fd_, buffer.data(), buffer.size(), 0,
                                     reinterpret_cast<sockaddr*>(&source), &length);
        if (size <= 0) {
            return std::nullopt;
        }
        if (from != nullptr) {
            *from = source;
        }
        return buffer[0];
    }

private:
    int fd_ = -1;
};

class UdpFaultProxyTest : public ::testing::Test {
protected:
    void create(const netsim::FaultProfile& to_upstream, const netsim::FaultProfile& to_client) {
        UdpFaultProxyConfig config;
        config.listen = TestSocket::loopback(0);
        config.upstream = upstream_.address();
        config.to_upstream = to_upstream;
        config.to_client = to_client;
        auto created = UdpFaultProxy::create(config);
        ASSERT_TRUE(created);
        proxy_ = std::move(created).value();
    }

    // duration の間、プロキシを動かす。
    void pump(milliseconds duration) {
        const auto until = Clock::now() + duration;
        while (Clock::now() < until) {
            proxy_->poll_once(milliseconds{1});
        }
    }

    TestSocket client_;
    TestSocket upstream_;
    std::unique_ptr<UdpFaultProxy> proxy_;
};

TEST_F(UdpFaultProxyTest, RelaysBothDirections) {
    create({}, {});
    client_.send_to(proxy_->listen_address(), 7);
    pump(milliseconds{50});
    sockaddr_in proxy_side{};
    ASSERT_EQ(upstream_.receive(&proxy_side), 7);
    upstream_.send_to(proxy_side, 8);
    pump(milliseconds{50});
    sockaddr_in reply_from{};
    ASSERT_EQ(client_.receive(&reply_from), 8);
    // 応答は待ち受けのアドレスから返る。
    EXPECT_EQ(reply_from.sin_port, proxy_->listen_address().sin_port);
    EXPECT_EQ(proxy_->stats().clients, 1U);
}

TEST_F(UdpFaultProxyTest, AppliesDelayAndLossToUpstreamDirection) {
    netsim::FaultProfile faults;
    faults.delay = milliseconds{40};
    faults.loss = 0.5;
    faults.seed = 9;
    create(faults, {});
    const auto start = Clock::now();
    for (std::uint8_t i = 0; i < 100; ++i) {
        client_.send_to(proxy_->listen_address(), i);
    }
    pump(milliseconds{20});
    EXPECT_FALSE(upstream_.receive().has_value()) << "delivered before the configured delay";
    pump(milliseconds{80});
    int received = 0;
    while (upstream_.receive()) {
        ++received;
    }
    EXPECT_GE(Clock::now() - start, milliseconds{40});
    EXPECT_EQ(static_cast<std::uint64_t>(received), proxy_->stats().forwarded);
    EXPECT_EQ(proxy_->stats().lost + proxy_->stats().forwarded, 100U);
    EXPECT_GT(received, 30);
    EXPECT_LT(received, 70);
}

TEST(UdpFaultProxyParseTest, ParsesIpv4Endpoint) {
    const auto endpoint = parse_ipv4_endpoint("10.0.0.2:3478");
    ASSERT_TRUE(endpoint);
    EXPECT_EQ(ntohs(endpoint->sin_port), 3478);
    EXPECT_FALSE(parse_ipv4_endpoint("10.0.0.2"));
    EXPECT_FALSE(parse_ipv4_endpoint("10.0.0.2:70000"));
    EXPECT_FALSE(parse_ipv4_endpoint("host:1"));
}

}  // namespace
}  // namespace vocroz::tools
