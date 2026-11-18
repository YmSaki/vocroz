// SPDX-License-Identifier: AGPL-3.0-only
//
// 光と音を検出して、対応したイベントのずれを集計します。
#include "av_sync_detector.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <numeric>
#include <set>

namespace vocroz::tools {
namespace {
// 時刻の単位を秒からミリ秒に換算します。
constexpr double kMillisecondsPerSecond = 1000;
// 絶対値の分布で 95% を覆う最近傍順位を使います。
constexpr double kPercentile = 0.95;
}  // namespace
std::vector<double> detect_audio_onsets_ms(std::span<const float> samples, int rate,
                                           const AvSyncDetectorSettings& settings) {
    std::vector<double> times;
    double next = 0;
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const double t = static_cast<double>(i) * kMillisecondsPerSecond / rate;
        // ホールドオフ中の正弦波の山を別のクリックとして数えません。
        if (t >= next && std::abs(samples[i]) >= settings.audio_threshold) {
            times.push_back(t);
            next = t + settings.holdoff_ms;
        }
    }
    return times;
}
std::vector<double> detect_video_onsets_ms(std::span<const std::uint8_t> frames, double fps,
                                           const AvSyncDetectorSettings& settings) {
    std::vector<double> times;
    bool lit = false;
    for (std::size_t i = 0; i < frames.size(); ++i) {
        const bool now = frames[i] >= settings.video_threshold;
        if (now && !lit)
            times.push_back(static_cast<double>(i) * kMillisecondsPerSecond / fps);
        lit = now;
    }
    return times;
}
AvSyncReport pair_onsets(std::span<const double> video, std::span<const double> audio,
                         const AvSyncDetectorSettings& settings) {
    std::multiset<double> remaining(audio.begin(), audio.end());
    AvSyncReport report;
    for (const auto t : video) {
        auto closest = remaining.lower_bound(t);
        // 前後の未使用の音を比較し、同じ距離なら先に鳴った音を選びます。
        if (closest != remaining.begin() &&
            (closest == remaining.end() || t - *std::prev(closest) <= *closest - t))
            --closest;
        if (closest == remaining.end() || std::abs(*closest - t) > settings.max_pair_distance_ms) {
            ++report.unmatched_video;
        } else {
            report.offsets_ms.push_back(*closest - t);
            remaining.erase(closest);
        }
    }
    report.unmatched_audio = remaining.size();
    return report;
}
AvSyncStats summarize(std::span<const double> offsets) {
    AvSyncStats stats;
    if (offsets.empty())
        return stats;
    std::vector<double> sorted(offsets.begin(), offsets.end());
    std::sort(sorted.begin(), sorted.end());
    stats.count = sorted.size();
    stats.min_ms = sorted.front();
    stats.max_ms = sorted.back();
    stats.mean_ms =
        std::accumulate(sorted.begin(), sorted.end(), 0.0) / static_cast<double>(stats.count);
    stats.median_ms = std::midpoint(sorted[(stats.count - 1) / 2], sorted[stats.count / 2]);
    for (double& value : sorted)
        value = std::abs(value);
    std::sort(sorted.begin(), sorted.end());
    stats.p95_abs_ms =
        sorted[static_cast<std::size_t>(std::ceil(kPercentile * static_cast<double>(stats.count))) -
               1];
    return stats;
}
}  // namespace vocroz::tools
