// SPDX-License-Identifier: AGPL-3.0-only
//
// PCP と NAT-PMP によるポート転送の手順のテスト。ゲートウェイは FakeGatewayChannel で置き換える。

#include "pcp_port_mapper.h"

#include <gtest/gtest.h>

#include "detail/random_utils.h"
#include "fakes/fake_gateway_channel.h"
#include "fakes/fake_random_source.h"
#include "gateway_responses.h"

namespace vocroz::network {
namespace {

using namespace std::chrono_literals;

constexpr std::uint16_t kInternalPort = 40000;
const UdpAddress kExternal = UdpAddress::from_ipv4({203, 0, 113, 7}, 40000);

class PcpPortMapperTest : public ::testing::Test {
protected:
    PcpPortMapperTest() {
        random_.value = 0x0c0b0a09;
        transport::FakeRandomSource copy;
        copy.value = random_.value;
        nonce_ = detail::random_bytes<12>(copy);
    }

    // map() のたびに同じ FakeGatewayChannel を使えるよう、所有しない形で渡す。
    PcpPortMapper make_mapper() {
        return PcpPortMapper(
            [this]() -> common::Result<std::unique_ptr<GatewayChannel>, NetworkError> {
                ++opens_;
                if (no_gateway_) {
                    return common::make_unexpected(NetworkError::kNoGateway);
                }
                return std::unique_ptr<GatewayChannel>(new Borrowed(channel_));
            },
            random_);
    }

    void reply(std::vector<std::uint8_t> bytes) {
        channel_.replies.push_back({std::move(bytes), NetworkError::kTimedOut});
    }

    void time_out() {
        channel_.replies.push_back({{}, NetworkError::kTimedOut});
    }

    // テストが持つ FakeGatewayChannel に処理を委ねる GatewayChannel。
    class Borrowed final : public GatewayChannel {
    public:
        explicit Borrowed(FakeGatewayChannel& inner) : inner_(inner) {}
        [[nodiscard]] UdpAddress local_address() const noexcept override {
            return inner_.local_address();
        }
        [[nodiscard]] common::Result<std::size_t, NetworkError> exchange(
            std::span<const std::uint8_t> request, std::span<std::uint8_t> response,
            std::chrono::milliseconds timeout) override {
            return inner_.exchange(request, response, timeout);
        }

    private:
        FakeGatewayChannel& inner_;
    };

    FakeGatewayChannel channel_;
    transport::FakeRandomSource random_;
    wire::PcpNonce nonce_{};
    int opens_ = 0;
    bool no_gateway_ = false;
};

TEST_F(PcpPortMapperTest, MapsWithPcp) {
    reply(test::pcp_map_response(0, nonce_, kInternalPort, kExternal, 3600));
    auto mapper = make_mapper();
    const auto mapped = mapper.map(kInternalPort, 3600s);
    ASSERT_TRUE(mapped);
    EXPECT_EQ(mapped.value().external, kExternal);
    EXPECT_EQ(mapped.value().lifetime, 3600s);
    EXPECT_EQ(mapped.value().method, PortMappingMethod::kPcp);
    ASSERT_EQ(channel_.requests.size(), 1u);
    EXPECT_EQ(channel_.requests[0][0], 2);  // PCP のバージョン
}

TEST_F(PcpPortMapperTest, RetriesWithDoublingTimeouts) {
    time_out();
    time_out();
    reply(test::pcp_map_response(0, nonce_, kInternalPort, kExternal, 3600));
    auto mapper = make_mapper();
    ASSERT_TRUE(mapper.map(kInternalPort, 3600s));
    EXPECT_EQ(channel_.timeouts, (std::vector<std::chrono::milliseconds>{250ms, 500ms, 1000ms}));
}

TEST_F(PcpPortMapperTest, GivesUpAfterThreeTimeouts) {
    auto mapper = make_mapper();
    const auto mapped = mapper.map(kInternalPort, 3600s);
    ASSERT_FALSE(mapped);
    EXPECT_EQ(mapped.error(), NetworkError::kTimedOut);
    EXPECT_EQ(channel_.requests.size(), 3u);
}

TEST_F(PcpPortMapperTest, IgnoresResponseWithAnotherNonce) {
    wire::PcpNonce other = nonce_;
    other[0] ^= 0xff;
    reply(test::pcp_map_response(0, other, kInternalPort, kExternal, 3600));
    reply(test::pcp_map_response(0, nonce_, kInternalPort, kExternal, 3600));
    auto mapper = make_mapper();
    EXPECT_TRUE(mapper.map(kInternalPort, 3600s));
}

TEST_F(PcpPortMapperTest, FallsBackToNatPmp) {
    reply(test::nat_pmp_unsupported_version());
    reply(test::nat_pmp_external_address(0, {203, 0, 113, 7}));
    reply(test::nat_pmp_map(0, kInternalPort, 40002, 7200));
    auto mapper = make_mapper();
    const auto mapped = mapper.map(kInternalPort, 3600s);
    ASSERT_TRUE(mapped);
    EXPECT_EQ(mapped.value().external, UdpAddress::from_ipv4({203, 0, 113, 7}, 40002));
    EXPECT_EQ(mapped.value().lifetime, 7200s);
    EXPECT_EQ(mapped.value().method, PortMappingMethod::kNatPmp);

    // 更新では PCP を送らず、前回の外部ポートを希望する。
    channel_.requests.clear();
    reply(test::nat_pmp_external_address(0, {203, 0, 113, 7}));
    reply(test::nat_pmp_map(0, kInternalPort, 40002, 7200));
    ASSERT_TRUE(mapper.map(kInternalPort, 3600s));
    ASSERT_EQ(channel_.requests.size(), 2u);
    EXPECT_EQ(channel_.requests[0][0], 0);
    EXPECT_EQ(channel_.requests[1][6], 0x9c);  // 40002 = 0x9c42
    EXPECT_EQ(channel_.requests[1][7], 0x42);
}

TEST_F(PcpPortMapperTest, TreatsPrivateExternalAddressAsUnsupported) {
    // ゲートウェイの外にもう 1 段 NAT がある（外部アドレスが CGNAT の共有アドレス）。
    reply(test::pcp_map_response(0, nonce_, kInternalPort,
                                 UdpAddress::from_ipv4({100, 64, 0, 5}, 40000), 3600));
    auto mapper = make_mapper();
    const auto mapped = mapper.map(kInternalPort, 3600s);
    ASSERT_FALSE(mapped);
    EXPECT_EQ(mapped.error(), NetworkError::kPortMappingUnsupported);
}

TEST_F(PcpPortMapperTest, ReportsRefusal) {
    reply(test::pcp_map_response(8, nonce_, kInternalPort, kExternal, 0));  // NO_RESOURCES
    auto mapper = make_mapper();
    const auto mapped = mapper.map(kInternalPort, 3600s);
    ASSERT_FALSE(mapped);
    EXPECT_EQ(mapped.error(), NetworkError::kPortMappingRefused);
}

TEST_F(PcpPortMapperTest, ReportsMissingGateway) {
    no_gateway_ = true;
    auto mapper = make_mapper();
    const auto mapped = mapper.map(kInternalPort, 3600s);
    ASSERT_FALSE(mapped);
    EXPECT_EQ(mapped.error(), NetworkError::kNoGateway);
}

TEST_F(PcpPortMapperTest, UnmapSendsZeroLifetimeWithSameNonce) {
    reply(test::pcp_map_response(0, nonce_, kInternalPort, kExternal, 3600));
    auto mapper = make_mapper();
    ASSERT_TRUE(mapper.map(kInternalPort, 3600s));
    const auto created = channel_.requests[0];
    mapper.unmap();
    ASSERT_EQ(channel_.requests.size(), 2u);
    const auto& removal = channel_.requests[1];
    EXPECT_EQ(std::vector<std::uint8_t>(removal.begin() + 4, removal.begin() + 8),
              (std::vector<std::uint8_t>{0, 0, 0, 0}));
    EXPECT_TRUE(std::equal(created.begin() + 24, created.begin() + 36, removal.begin() + 24));
    // 解除済みなら何も送らない。
    mapper.unmap();
    EXPECT_EQ(channel_.requests.size(), 2u);
}

}  // namespace
}  // namespace vocroz::network
