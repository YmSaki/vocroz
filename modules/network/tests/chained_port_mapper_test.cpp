// SPDX-License-Identifier: AGPL-3.0-only
//
// 複数のポート開放の方法を順に試す PortMapper のテスト。

#include "chained_port_mapper.h"

#include <gtest/gtest.h>

#include "fakes/fake_port_mapper.h"

namespace vocroz::network {
namespace {

using namespace std::chrono_literals;

class ChainedPortMapperTest : public ::testing::Test {
protected:
    ChainedPortMapperTest() {
        auto first = std::make_unique<FakePortMapper>();
        auto second = std::make_unique<FakePortMapper>();
        first_ = first.get();
        second_ = second.get();
        std::vector<std::unique_ptr<PortMapper>> mappers;
        mappers.push_back(std::move(first));
        mappers.push_back(std::move(second));
        chain_ = std::make_unique<ChainedPortMapper>(std::move(mappers));
    }

    FakePortMapper* first_ = nullptr;
    FakePortMapper* second_ = nullptr;
    std::unique_ptr<ChainedPortMapper> chain_;
};

TEST_F(ChainedPortMapperTest, UsesFirstSuccessfulMapperAndKeepsIt) {
    second_->succeed = true;
    second_->mapping.method = PortMappingMethod::kUpnpIgd;
    const auto mapped = chain_->map(40000, 3600s);
    ASSERT_TRUE(mapped);
    EXPECT_EQ(mapped.value().method, PortMappingMethod::kUpnpIgd);
    EXPECT_EQ(first_->map_calls, 1);

    // 更新は成功した方法だけで行う。
    ASSERT_TRUE(chain_->map(40000, 3600s));
    EXPECT_EQ(first_->map_calls, 1);
    EXPECT_EQ(second_->map_calls, 2);

    chain_->unmap();
    EXPECT_EQ(first_->unmap_calls, 0);
    EXPECT_EQ(second_->unmap_calls, 1);
}

TEST_F(ChainedPortMapperTest, RetriesAllMappersWhenRenewalFails) {
    second_->succeed = true;
    ASSERT_TRUE(chain_->map(40000, 3600s));
    second_->succeed = false;
    first_->succeed = true;
    ASSERT_TRUE(chain_->map(40000, 3600s));
    EXPECT_EQ(first_->map_calls, 2);
}

TEST_F(ChainedPortMapperTest, ReturnsLastErrorWhenAllFail) {
    first_->error = NetworkError::kTimedOut;
    second_->error = NetworkError::kPortMappingRefused;
    const auto mapped = chain_->map(40000, 3600s);
    ASSERT_FALSE(mapped);
    EXPECT_EQ(mapped.error(), NetworkError::kPortMappingRefused);
    chain_->unmap();
    EXPECT_EQ(first_->unmap_calls + second_->unmap_calls, 0);
}

}  // namespace
}  // namespace vocroz::network
