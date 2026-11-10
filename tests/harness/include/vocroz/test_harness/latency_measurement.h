// SPDX-License-Identifier: AGPL-3.0-only
//
// 端から端までの片道の遅延（子機のキャプチャから親機の出力までか、親機のミックスから子機の出力
// までの返送）を、実際の伝送の部品を通して計る。
// 無音の間に正弦波のバーストを周期的に送り、出力でバーストの立ち上がりを検出して、
// 入力での立ち上がりとの時刻の差を遅延とする。デバイスの入出力の遅延（ドライバーのバッファ）は
// シミュレーションの外なので、設定した値を足して「口から耳まで」の見積もりにする。
// 合奏の目標を満たすかを、回線の条件ごとに CI と手元で確かめるために使う
// （docs/adr/0025-integration-test-harness.md）。
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "vocroz/common/result.h"
#include "vocroz/netsim/simulated_network.h"
#include "vocroz/test_harness/media_link_simulation.h"
#include "vocroz/transport/playout_stream.h"

namespace vocroz::test_harness {

/// 遅延計測の条件。
struct LatencyScenario {
    /// 伝送の構成と回線の障害。
    MediaLinkConfig link;
    /// 計測する長さ。
    std::chrono::milliseconds duration{10'000};
    /// 送信側の入力デバイスの遅延（シミュレーションの外。見積もりに足す）。上りは子機のマイク。
    /// 返送はミックスを作るところから計るので、ふつうは 0 にする。
    std::chrono::microseconds device_input_latency{0};
    /// 受信側の出力デバイスの遅延（シミュレーションの外。見積もりに足す）。上りは親機の出力、
    /// 返送は子機のモニターの出力。
    std::chrono::microseconds device_output_latency{0};
};

/// 構成から計算した遅延の内訳（ミリ秒）。計測値がこの範囲から外れたら、想定外のバッファがある。
struct LatencyBudget {
    double device_input_ms = 0.0;   ///< 入力デバイス
    double capture_block_ms = 0.0;  ///< キャプチャのブロックがたまるまで（最大）
    double frame_ms = 0.0;          ///< 1 フレームがたまるまで（最大）
    double codec_lookahead_ms = 0.0;    ///< Opus の先読み
    double network_min_ms = 0.0;        ///< 片道の固定の遅延
    double network_max_ms = 0.0;        ///< 片道の固定の遅延と揺らぎの幅
    double jitter_buffer_min_ms = 0.0;  ///< ジッターバッファの目標の下限
    double jitter_buffer_max_ms = 0.0;  ///< ジッターバッファの目標の上限
    double output_block_ms = 0.0;       ///< 出力のブロック 1 つ分（最大）
    double device_output_ms = 0.0;      ///< 出力デバイス
    double total_min_ms = 0.0;  ///< 合計の下限（ブロックとフレームの待ちを 0 とした場合）
    double total_max_ms = 0.0;  ///< 合計の上限
};

/// 遅延計測の結果。
struct LatencyReport {
    /// 検出したバーストごとの遅延（ミリ秒）。デバイスの遅延を含む。
    std::vector<double> latencies_ms;
    std::size_t bursts_sent = 0;      ///< 送ったバースト
    std::size_t bursts_detected = 0;  ///< 出力で検出したバースト
    double min_ms = 0.0;              ///< 最小
    double median_ms = 0.0;           ///< 中央値
    double p95_ms = 0.0;              ///< 95 パーセンタイル
    double max_ms = 0.0;              ///< 最大
    LatencyBudget budget;             ///< 構成から計算した内訳
    transport::PlayoutStats playout;  ///< 計測の終わりの再生ストリームの統計
    MediaDirection direction = MediaDirection::kUplink;  ///< 計ったストリームの向き
    netsim::LinkStats path;  ///< 送信側から受信側への経路の統計
};

/// ストリームの向きを、計測ツールの出力に使う短い名前にする。
/// @param direction 向き
/// @return "uplink" か "return"
[[nodiscard]] const char* direction_label(MediaDirection direction) noexcept;

/// 構成から遅延の内訳を計算する。
/// @param scenario 条件
/// @param codec_lookahead_samples エンコーダーの先読み（OpusFrameEncoder::lookahead_samples()）
[[nodiscard]] LatencyBudget compute_latency_budget(const LatencyScenario& scenario,
                                                   std::size_t codec_lookahead_samples);

/// 遅延を計測する。
/// @param scenario 条件
/// @return 結果。構成が不正なら原因のメッセージ
[[nodiscard]] common::Result<LatencyReport, std::string> measure_latency(
    const LatencyScenario& scenario);

/// 結果を人が読む表にする（遅延計測ツールの出力とテストの失敗時のログに使う）。
/// @param report 結果
[[nodiscard]] std::string format_latency_report(const LatencyReport& report);

}  // namespace vocroz::test_harness
