// SPDX-License-Identifier: AGPL-3.0-only
//
// vocroz-latency: 端から端までの片道の遅延の計測ツール。
// 子機から親機への上り（既定）と、親機から子機への返送モニター（--path return）を計れる。
// 回線の条件（遅延、ジッター、欠損など）と、フレーム長、ジッターバッファ、デバイスのブロック長を指定して、
// 実際の伝送の部品を通した遅延を仮想時刻で計り、内訳と並べて表示する。
// 合奏の目標に対して、どの設定ならどの回線まで間に合うかを、実機に触る前に見積もるために使う。
//
// 使い方:
//   vocroz-latency [--path uplink|return] [--net <障害の設定>] [--frame 2.5|5] [--min-delay <ms>]
//   [--max-delay <ms>]
//                  [--capture-block <samples>] [--output-block <samples>]
//                  [--device-in <ms>] [--device-out <ms>] [--seconds <s>]
//   障害の設定の書式は tests/README.md（例: --net delay=15ms,jitter=4ms,loss=0.01,seed=3）

#include <charconv>
#include <chrono>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include "vocroz/netsim/fault_profile.h"
#include "vocroz/test_harness/latency_measurement.h"

namespace {

using vocroz::test_harness::LatencyScenario;

constexpr int kExitUsage = 2;

std::optional<long long> parse_int(std::string_view text) {
    long long value = 0;
    const auto* const end = text.data() + text.size();
    const auto [ptr, ec] = std::from_chars(text.data(), end, value);
    if (ec != std::errc{} || ptr != end || value < 0) {
        return std::nullopt;
    }
    return value;
}

int usage(const char* message) {
    std::fprintf(stderr, "vocroz-latency: %s\n", message);
    std::fprintf(stderr,
                 "usage: vocroz-latency [--path uplink|return] [--net <faults>] [--frame 2.5|5] "
                 "[--min-delay <ms>] "
                 "[--max-delay <ms>] [--capture-block <samples>] [--output-block <samples>] "
                 "[--device-in <ms>] [--device-out <ms>] [--seconds <s>]\n");
    return kExitUsage;
}

// 引数を条件に反映する。失敗したら原因を返す。
std::optional<std::string> apply_option(std::string_view name, std::string_view value,
                                        LatencyScenario& scenario) {
    using std::chrono::milliseconds;
    if (name == "--path") {
        if (value == "uplink") {
            scenario.link.direction = vocroz::test_harness::MediaDirection::kUplink;
        } else if (value == "return") {
            scenario.link.direction = vocroz::test_harness::MediaDirection::kReturn;
        } else {
            return "--path must be uplink or return";
        }
        return std::nullopt;
    }
    if (name == "--net") {
        auto profile = vocroz::netsim::parse_fault_profile(value);
        if (!profile) {
            return profile.error();
        }
        scenario.link.faults = profile.value();
        return std::nullopt;
    }
    if (name == "--frame") {
        if (value == "2.5") {
            scenario.link.encoder.frame_duration = vocroz::transport::FrameDuration::k2500Us;
        } else if (value == "5") {
            scenario.link.encoder.frame_duration = vocroz::transport::FrameDuration::k5000Us;
        } else {
            return "--frame must be 2.5 or 5";
        }
        scenario.link.playout.frame_duration = scenario.link.encoder.frame_duration;
        return std::nullopt;
    }
    const auto number = parse_int(value);
    if (!number) {
        return std::string("invalid number for ") + std::string(name);
    }
    if (name == "--min-delay") {
        scenario.link.playout.min_delay = milliseconds{*number};
    } else if (name == "--max-delay") {
        scenario.link.playout.max_delay = milliseconds{*number};
    } else if (name == "--capture-block") {
        scenario.link.capture_block = static_cast<std::size_t>(*number);
    } else if (name == "--output-block") {
        scenario.link.output_block = static_cast<std::size_t>(*number);
    } else if (name == "--device-in") {
        scenario.device_input_latency = milliseconds{*number};
    } else if (name == "--device-out") {
        scenario.device_output_latency = milliseconds{*number};
    } else if (name == "--seconds") {
        scenario.duration = std::chrono::seconds{*number};
    } else {
        return std::string("unknown option: ") + std::string(name);
    }
    return std::nullopt;
}

}  // namespace

int main(int argc, char** argv) {
    LatencyScenario scenario;
    for (int i = 1; i < argc; i += 2) {
        if (i + 1 >= argc) {
            return usage("missing value for the last option");
        }
        if (const auto error = apply_option(argv[i], argv[i + 1], scenario)) {
            return usage(error->c_str());
        }
    }
    const auto report = vocroz::test_harness::measure_latency(scenario);
    if (!report) {
        return usage(report.error().c_str());
    }
    std::printf("path: %s\n", vocroz::test_harness::direction_label(scenario.link.direction));
    std::printf("network: %s\n",
                vocroz::netsim::format_fault_profile(scenario.link.faults).c_str());
    std::fputs(vocroz::test_harness::format_latency_report(report.value()).c_str(), stdout);
    return report.value().bursts_detected == 0 ? 1 : 0;
}
