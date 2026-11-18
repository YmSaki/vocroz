// SPDX-License-Identifier: AGPL-3.0-only
//
// 発光の時刻と生成した波形を検証します。
#include "av_sync_pattern.h"

#include <gtest/gtest.h>

namespace vocroz::tools {
namespace {
// 32 ビットで表せない発光時刻も正のまま保持します。
TEST(AvSyncPatternTest, KeepsLongEventTimes) {
    AvSyncPattern p;
    p.seconds = 3000002;
    p.interval_ms = 1200000000;
    p.fps = 1;
    p.flash_ms = 1000;
    ASSERT_TRUE(validate(p));
    EXPECT_EQ(event_times_ms(p), (std::vector<std::int64_t>{1200000000, 2400000000LL}));
}
// 設定の境界を検査します。
TEST(AvSyncPatternTest, ChecksSettings) {
    AvSyncPattern p;
    EXPECT_TRUE(validate(p));
    p.fps = 30;
    p.interval_ms = 1010;
    EXPECT_EQ(validate(p).error(), AvSyncPatternError::kNotFrameAligned);
    p = {};
    p.audio_offset_ms = 500;
    EXPECT_EQ(validate(p).error(), AvSyncPatternError::kOffsetTooLarge);
    p = {};
    p.width = 3;
    EXPECT_EQ(validate(p).error(), AvSyncPatternError::kInvalidValue);
}
// 既定値で光と音の境界が揃います。
TEST(AvSyncPatternTest, GeneratesAlignedSignals) {
    AvSyncPattern p;
    EXPECT_EQ(event_times_ms(p),
              (std::vector<std::int64_t>{1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000}));
    auto a = generate_audio(p);
    ASSERT_EQ(a.size(), 480000U);
    EXPECT_NEAR(a[47999], 0, 0.0001);
    EXPECT_GT(a[48001], 0);
    auto v = generate_frame_lumas(p);
    EXPECT_EQ(v[59], 0);
    EXPECT_EQ(v[60], 255);
    EXPECT_EQ(v[66], 0);
}
}  // namespace
}  // namespace vocroz::tools
