// SPDX-License-Identifier: AGPL-3.0-only
//
// 試験パターンの生成と計測の契約を検証します。
#include "av_sync_measurement.h"

#include <gtest/gtest.h>

#include "av_sync_pattern.h"
#include "wav_file.h"
#include "y4m_file.h"
namespace vocroz::tools {
namespace {
class AvSyncMeasurementTest : public ::testing::Test {
protected:
    std::filesystem::path dir = std::filesystem::temp_directory_path() /
                                (std::string("vocroz-AvSyncMeasurementTest-") +
                                 ::testing::UnitTest::GetInstance()->current_test_info()->name());
    AvSyncMeasurementTest() {
        std::filesystem::create_directories(dir);
    }
    ~AvSyncMeasurementTest() override {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
};
// 既知のずれとファイル別のエラーを確認します。
TEST_F(AvSyncMeasurementTest, MeasuresGeneratedFiles) {
    auto v = dir / "v";
    auto a = dir / "a";
    EXPECT_EQ(measure_files(v, a, {}).error(), AvSyncMeasurementError::kVideoFile);
    for (int offset : {40, -25}) {
        AvSyncPattern p;
        p.width = 4;
        p.height = 2;
        p.audio_offset_ms = offset;
        ASSERT_TRUE(write_y4m_flat(v, generate_frame_lumas(p), p.width, p.height, p.fps));
        EXPECT_EQ(measure_files(v, dir / "absent", {}).error(), AvSyncMeasurementError::kAudioFile);
        ASSERT_TRUE(write_wav_mono16(a, generate_audio(p), p.sample_rate));
        auto r = measure_files(v, a, {});
        ASSERT_TRUE(r);
        EXPECT_NEAR(r.value().stats.median_ms, offset, 0.5);
        EXPECT_EQ(r.value().stats.count, 9U);
        EXPECT_EQ(r.value().report.unmatched_video, 0U);
        EXPECT_EQ(r.value().report.unmatched_audio, 0U);
    }
}
}  // namespace
}  // namespace vocroz::tools
