// SPDX-License-Identifier: AGPL-3.0-only
//
// 遅延計測の実装。

#include "vocroz/test_harness/latency_measurement.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <optional>
#include <utility>

#include "vocroz/transport/constants.h"

namespace vocroz::test_harness {
namespace {

using std::chrono::duration;
using std::chrono::duration_cast;

// バーストの周期（サンプル数）。遅延の上限（ジッターバッファの上限は 250ms 程度まで）より長くし、
// 出力の立ち上がりがどの入力のバーストのものかを取り違えないようにする。
// フレーム長の倍数からずらし、フレームの中での立ち上がりの位置を毎回変える。
constexpr std::uint64_t kBurstPeriodSamples = 19'200 + 37;  // 約 400ms
// バーストの長さ。PLC で途切れても立ち上がりを検出できる程度の長さにする。
constexpr std::uint64_t kBurstLengthSamples = 1'920;  // 40ms
// 最初のバーストの位置。ジッターバッファが溜まり、再生が始まってから送る。
constexpr std::uint64_t kFirstBurstSamples = 9'600;  // 200ms
constexpr double kBurstFrequencyHz = 1'000.0;
constexpr double kBurstAmplitude = 0.5;
// 立ち上がりの判定のしきい値。Opus の量子化雑音（無音時は -60dBFS 程度）より十分大きく、
// バーストの振幅より十分小さい値にする。
constexpr float kOnsetThreshold = 0.1F;
constexpr float kQuietThreshold = 0.02F;
// この長さ静かだったあとに、しきい値を超えたら立ち上がりとみなす。
constexpr std::uint64_t kQuietBeforeOnsetSamples = 2'400;  // 50ms

double samples_to_ms(double samples) {
    return samples * 1'000.0 / transport::kSampleRate;
}

double to_ms(std::chrono::microseconds value) {
    return duration_cast<duration<double, std::milli>>(value).count();
}

bool in_burst(std::uint64_t sample) {
    return sample >= kFirstBurstSamples &&
           (sample - kFirstBurstSamples) % kBurstPeriodSamples < kBurstLengthSamples;
}

// burst 番目のバーストの立ち上がりのサンプル位置。
std::uint64_t burst_onset(std::uint64_t burst) {
    return kFirstBurstSamples + burst * kBurstPeriodSamples;
}

double percentile(std::vector<double> sorted, double fraction) {
    if (sorted.empty()) {
        return 0.0;
    }
    const auto index =
        static_cast<std::size_t>(std::ceil(fraction * static_cast<double>(sorted.size())) - 1.0);
    return sorted[std::min(index, sorted.size() - 1)];
}

// 出力の立ち上がりを検出する。
class OnsetDetector {
public:
    // 立ち上がりを検出したら、そのサンプル位置を返す。
    std::optional<std::uint64_t> feed(std::uint64_t position, float sample) {
        const float magnitude = std::fabs(sample);
        if (magnitude < kQuietThreshold) {
            ++quiet_run_;
            return std::nullopt;
        }
        const bool was_quiet = quiet_run_ >= kQuietBeforeOnsetSamples;
        if (magnitude < kOnsetThreshold) {
            return std::nullopt;
        }
        quiet_run_ = 0;
        if (was_quiet) {
            return position;
        }
        return std::nullopt;
    }

private:
    std::uint64_t quiet_run_ = 0;
};

}  // namespace

LatencyBudget compute_latency_budget(const LatencyScenario& scenario,
                                     std::size_t codec_lookahead_samples) {
    const auto& link = scenario.link;
    LatencyBudget budget;
    budget.device_input_ms = to_ms(scenario.device_input_latency);
    budget.capture_block_ms = samples_to_ms(static_cast<double>(link.capture_block));
    budget.frame_ms =
        samples_to_ms(static_cast<double>(transport::frame_samples(link.encoder.frame_duration)));
    budget.codec_lookahead_ms = samples_to_ms(static_cast<double>(codec_lookahead_samples));
    budget.network_min_ms = to_ms(link.uplink.delay);
    budget.network_max_ms = to_ms(link.uplink.delay + link.uplink.jitter);
    budget.jitter_buffer_min_ms = to_ms(link.playout.min_delay);
    budget.jitter_buffer_max_ms = to_ms(link.playout.max_delay);
    budget.output_block_ms = samples_to_ms(static_cast<double>(link.output_block));
    budget.device_output_ms = to_ms(scenario.device_output_latency);
    budget.total_min_ms = budget.device_input_ms + budget.codec_lookahead_ms +
                          budget.network_min_ms + budget.device_output_ms;
    budget.total_max_ms = budget.device_input_ms + budget.capture_block_ms + budget.frame_ms +
                          budget.codec_lookahead_ms + budget.network_max_ms +
                          budget.jitter_buffer_max_ms + budget.output_block_ms +
                          budget.device_output_ms;
    return budget;
}

common::Result<LatencyReport, std::string> measure_latency(const LatencyScenario& scenario) {
    auto created = MediaLinkSimulation::create(scenario.link);
    if (!created) {
        return common::make_unexpected(created.error());
    }
    auto& simulation = *created.value();
    const auto total_samples = static_cast<std::uint64_t>(
        scenario.duration.count() * static_cast<std::int64_t>(transport::kSampleRate) / 1'000);

    OnsetDetector detector;
    std::vector<std::uint64_t> output_onsets;
    const auto source = [](std::uint64_t first, std::span<float> block) {
        for (std::size_t i = 0; i < block.size(); ++i) {
            const std::uint64_t position = first + i;
            const double t = static_cast<double>(position) / transport::kSampleRate;
            block[i] = in_burst(position)
                           ? static_cast<float>(kBurstAmplitude * std::sin(2.0 * std::numbers::pi *
                                                                           kBurstFrequencyHz * t))
                           : 0.0F;
        }
    };
    const auto sink = [&](std::uint64_t first, std::span<const float> block) {
        for (std::size_t i = 0; i < block.size(); ++i) {
            if (const auto onset = detector.feed(first + i, block[i])) {
                output_onsets.push_back(*onset);
            }
        }
    };
    simulation.run(total_samples, source, sink);

    LatencyReport report;
    // 計測の終わりまでに出力へ届き得たバーストだけを数える。
    const double device_ms = to_ms(scenario.device_input_latency + scenario.device_output_latency);
    report.budget = compute_latency_budget(scenario, simulation.sender().lookahead_samples());
    const auto max_latency_samples =
        static_cast<std::uint64_t>(report.budget.total_max_ms * transport::kSampleRate / 1'000.0);
    for (std::uint64_t burst = 0; burst_onset(burst) + max_latency_samples < total_samples;
         ++burst) {
        ++report.bursts_sent;
    }
    std::size_t next_burst = 0;
    for (const std::uint64_t onset : output_onsets) {
        // 出力の立ち上がりより前で、最も新しい入力のバーストに対応づける。
        while (next_burst + 1 < report.bursts_sent && burst_onset(next_burst + 1) <= onset) {
            ++next_burst;
        }
        if (next_burst >= report.bursts_sent || burst_onset(next_burst) > onset) {
            continue;
        }
        report.latencies_ms.push_back(
            samples_to_ms(static_cast<double>(onset - burst_onset(next_burst))) + device_ms);
        // 1 つのバーストを二重に数えない。
        ++next_burst;
    }
    report.bursts_detected = report.latencies_ms.size();
    auto sorted = report.latencies_ms;
    std::sort(sorted.begin(), sorted.end());
    if (!sorted.empty()) {
        report.min_ms = sorted.front();
        report.max_ms = sorted.back();
        report.median_ms = percentile(sorted, 0.5);
        report.p95_ms = percentile(sorted, 0.95);
    }
    report.playout = simulation.receiver().playout_stats();
    report.uplink = simulation.network().stats(MediaLinkSimulation::kClientAddress,
                                               MediaLinkSimulation::kHostAddress);
    return report;
}

std::string format_latency_report(const LatencyReport& report) {
    const auto& b = report.budget;
    std::string out;
    char line[160];
    const auto add = [&](const char* label, double value) {
        std::snprintf(line, sizeof(line), "  %-28s %8.2f ms\n", label, value);
        out += line;
    };
    std::snprintf(line, sizeof(line), "measured (%zu of %zu bursts)\n", report.bursts_detected,
                  report.bursts_sent);
    out += line;
    add("min", report.min_ms);
    add("median", report.median_ms);
    add("p95", report.p95_ms);
    add("max", report.max_ms);
    out += "budget\n";
    add("device input", b.device_input_ms);
    add("capture block (max)", b.capture_block_ms);
    add("frame (max)", b.frame_ms);
    add("codec lookahead", b.codec_lookahead_ms);
    add("network min", b.network_min_ms);
    add("network max", b.network_max_ms);
    add("jitter buffer min", b.jitter_buffer_min_ms);
    add("jitter buffer max", b.jitter_buffer_max_ms);
    add("output block (max)", b.output_block_ms);
    add("device output", b.device_output_ms);
    add("total min", b.total_min_ms);
    add("total max", b.total_max_ms);
    out += "playout\n";
    std::snprintf(line, sizeof(line),
                  "  decoded %llu, concealed %llu, late %llu, duplicate %llu, underruns %llu, "
                  "target %.2f ms\n",
                  static_cast<unsigned long long>(report.playout.frames_decoded),
                  static_cast<unsigned long long>(report.playout.frames_concealed),
                  static_cast<unsigned long long>(report.playout.frames_late),
                  static_cast<unsigned long long>(report.playout.frames_duplicate),
                  static_cast<unsigned long long>(report.playout.underruns),
                  samples_to_ms(static_cast<double>(report.playout.target_delay_samples)));
    out += line;
    std::snprintf(line, sizeof(line),
                  "uplink\n  sent %llu, lost %llu, duplicated %llu, reordered %llu\n",
                  static_cast<unsigned long long>(report.uplink.sent),
                  static_cast<unsigned long long>(report.uplink.lost),
                  static_cast<unsigned long long>(report.uplink.duplicated),
                  static_cast<unsigned long long>(report.uplink.reordered));
    out += line;
    return out;
}

}  // namespace vocroz::test_harness
