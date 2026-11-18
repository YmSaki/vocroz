// SPDX-License-Identifier: AGPL-3.0-only
//
// 試験パターンの生成と計測の契約を検証します。
#include "av_sync_detector.h"

#include <algorithm>

#include <gtest/gtest.h>

#include "av_sync_pattern.h"

namespace vocroz::tools {
namespace {
// 音の遅れと進みを対応づけます。
TEST(AvSyncDetectorTest, MeasuresOffsets) {
    for (int offset : {0, 40, -25}) {
        AvSyncPattern p;
        p.audio_offset_ms = offset;
        auto a = detect_audio_onsets_ms(generate_audio(p), p.sample_rate, {});
        auto v = detect_video_onsets_ms(generate_frame_lumas(p), p.fps, {});
        auto report = pair_onsets(v, a, {});
        ASSERT_EQ(report.offsets_ms.size(), 9U);
        for (double x : report.offsets_ms)
            EXPECT_NEAR(x, offset, 0.5);
    }
}
// 生の素材から光またはクリックを一回除いて、検出漏れの方向を確認します。
TEST(AvSyncDetectorTest, CountsMissingSignals) {
    AvSyncPattern pattern;
    auto frames = generate_frame_lumas(pattern);
    auto samples = generate_audio(pattern);
    const auto video = detect_video_onsets_ms(frames, pattern.fps, {});
    const auto audio = detect_audio_onsets_ms(samples, pattern.sample_rate, {});
    std::fill_n(frames.begin() + pattern.fps, pattern.flash_ms * pattern.fps / 1000, 0);
    std::fill_n(samples.begin() + pattern.sample_rate,
                pattern.click_ms * pattern.sample_rate / 1000, 0);
    const auto missing_flash =
        pair_onsets(detect_video_onsets_ms(frames, pattern.fps, {}), audio, {});
    EXPECT_EQ(missing_flash.unmatched_audio, 1U);
    EXPECT_EQ(missing_flash.offsets_ms.size(), 8U);
    EXPECT_EQ(pair_onsets(video, detect_audio_onsets_ms(samples, pattern.sample_rate, {}), {})
                  .unmatched_video,
              1U);
}
// 符号付きの中央値と絶対値の順位を検査します。
TEST(AvSyncDetectorTest, SummarizesSignedOffsets) {
    const double a[]{-10, 0, 10, 20};
    auto s = summarize(a);
    EXPECT_NEAR(s.min_ms, -10, 0.001);
    EXPECT_NEAR(s.max_ms, 20, 0.001);
    EXPECT_NEAR(s.mean_ms, 5, 0.001);
    EXPECT_NEAR(s.median_ms, 5, 0.001);
    EXPECT_NEAR(s.p95_abs_ms, 20, 0.001);
    EXPECT_EQ(summarize({}).count, 0U);
}
}  // namespace
}  // namespace vocroz::tools
