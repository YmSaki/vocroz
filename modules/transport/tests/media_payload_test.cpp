// SPDX-License-Identifier: AGPL-3.0-only
//
// decode_media_payload() の単体テスト。

#include "vocroz/transport/media_payload.h"

#include <array>
#include <cstdint>

#include <gtest/gtest.h>

namespace vocroz::transport {
namespace {

TEST(MediaPayloadTest, DecodesFieldsAndPointsIntoPlaintext) {
    // ストリーム ID 0x0102、タイムスタンプ 0x030405060708090A、epoch 0x0B0C0D0E、Opus 2 バイト。
    const std::array<std::uint8_t, kMediaPayloadHeaderBytes + 2> plaintext{
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0xAA, 0xBB};
    const auto payload = decode_media_payload(plaintext);
    ASSERT_TRUE(payload);
    EXPECT_EQ(payload.value().stream_id, 0x0102U);
    EXPECT_EQ(payload.value().timestamp, 0x030405060708090AULL);
    EXPECT_EQ(payload.value().mute_epoch, 0x0B0C0D0EU);
    ASSERT_EQ(payload.value().opus_payload.size(), 2U);
    EXPECT_EQ(payload.value().opus_payload.data(), plaintext.data() + kMediaPayloadHeaderBytes);
}

TEST(MediaPayloadTest, RejectsPlaintextWithoutOpusPayload) {
    const std::array<std::uint8_t, kMediaPayloadHeaderBytes> plaintext{};
    const auto payload = decode_media_payload(plaintext);
    ASSERT_FALSE(payload);
    EXPECT_EQ(payload.error(), TransportError::kMalformedPayload);
}

TEST(MediaPayloadTest, RejectsTruncatedFixedPart) {
    const std::array<std::uint8_t, 3> plaintext{};
    const auto payload = decode_media_payload(plaintext);
    ASSERT_FALSE(payload);
    EXPECT_EQ(payload.error(), TransportError::kMalformedPayload);
}

}  // namespace
}  // namespace vocroz::transport
