// SPDX-License-Identifier: AGPL-3.0-only
//
// 検出のしきい値。
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace vocroz::tools {
/// 検出のしきい値。
struct AvSyncDetectorSettings {
    /// |サンプル| がこれ以上で音の立ち上がりとみなす
    float audio_threshold = 0.05F;
    /// フレームの平均の明るさがこれ以上で光っているとみなす
    std::uint8_t video_threshold = 128;
    /// 音の立ち上がりを 1 つ見つけたら、この間は次を探さない
    double holdoff_ms = 500.0;
    /// これより離れた音と映像は対応づけない
    double max_pair_distance_ms = 500.0;
};

/// 計測の結果。
struct AvSyncReport {
    /// 対応づけた組ごとのずれ（音声 − 映像）。映像の時刻の順
    std::vector<double> offsets_ms;
    /// 対応する音がなかった光
    std::size_t unmatched_video = 0;
    /// 対応する光がなかった音
    std::size_t unmatched_audio = 0;
};

/// ずれの統計。offsets_ms が空なら count だけが 0 で、ほかは 0.0。
struct AvSyncStats {
    std::size_t count = 0;
    double min_ms = 0.0;
    double max_ms = 0.0;
    double mean_ms = 0.0;
    /// 個数が偶数なら中央の 2 つの平均
    double median_ms = 0.0;
    /// |ずれ| の 95 パーセンタイル（最近傍順位法: 昇順に並べた ceil(0.95 * count) 番目、1 始まり）
    double p95_abs_ms = 0.0;
};

/// 振幅がしきい値に達したサンプルの時刻をミリ秒で返します。
[[nodiscard]] std::vector<double> detect_audio_onsets_ms(std::span<const float> samples,
                                                         int sample_rate,
                                                         const AvSyncDetectorSettings& settings);
/// 暗から明に変わったフレームの時刻をミリ秒で返します。
[[nodiscard]] std::vector<double> detect_video_onsets_ms(std::span<const std::uint8_t> frame_lumas,
                                                         double fps,
                                                         const AvSyncDetectorSettings& settings);
/// 時刻順の光に最も近い未使用の音を対応づけます。
[[nodiscard]] AvSyncReport pair_onsets(std::span<const double> video_onsets_ms,
                                       std::span<const double> audio_onsets_ms,
                                       const AvSyncDetectorSettings& settings);
/// 音声から映像を引いた時間差をミリ秒で集計します。
[[nodiscard]] AvSyncStats summarize(std::span<const double> offsets_ms);
}  // namespace vocroz::tools
