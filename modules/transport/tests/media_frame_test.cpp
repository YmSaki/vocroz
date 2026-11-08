// SPDX-License-Identifier: AGPL-3.0-only
//
// make_media_frame() の単体テスト。ペイロードの長さの検証とコピーを確かめる。

#include "vocroz/transport/media_frame.h"

#include <array>
#include <vector>

#include <gtest/gtest.h>

namespace vocroz::transport {
namespace {

TEST(MediaFrameTest, CopiesFieldsAndPayload) {
    const std::array<std::uint8_t, 3> payload{1, 2, 3};
    const auto frame = make_media_frame(480, 7, payload);
    ASSERT_TRUE(frame);
    EXPECT_EQ(frame.value().timestamp, 480U);
    EXPECT_EQ(frame.value().mute_epoch, 7U);
    ASSERT_EQ(frame.value().payload_bytes().size(), 3U);
    EXPECT_EQ(frame.value().payload_bytes()[2], 3);
}

TEST(MediaFrameTest, RejectsEmptyPayload) {
    const auto frame = make_media_frame(0, 1, {});
    ASSERT_FALSE(frame);
    EXPECT_EQ(frame.error(), MediaFrameError::kEmptyPayload);
}

TEST(MediaFrameTest, RejectsPayloadLongerThanLimit) {
    const std::vector<std::uint8_t> payload(kMaxOpusPayloadBytes + 1, 0);
    const auto frame = make_media_frame(0, 1, payload);
    ASSERT_FALSE(frame);
    EXPECT_EQ(frame.error(), MediaFrameError::kPayloadTooLarge);
}

TEST(MediaFrameTest, AcceptsPayloadAtLimit) {
    const std::vector<std::uint8_t> payload(kMaxOpusPayloadBytes, 0);
    EXPECT_TRUE(make_media_frame(0, 1, payload));
}

}  // namespace
}  // namespace vocroz::transport
