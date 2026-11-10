// SPDX-License-Identifier: AGPL-3.0-only
//
// 子機 1 台と親機の間の 1 ストリーム（子機から親機への上りか、親機から子機への返送）を、
// 仮想時刻の上で端から端まで動かす。
// キャプチャのコールバック（送信側）、シミュレーションのネットワーク、オーディオ出力のコールバック
// （受信側）を、サンプル単位の仮想時刻で順に進める。フェイクデバイスの代わりに、キャプチャの中身は
// Source が作り、出力は Sink が受け取る。
// 障害注入の結合テストと、遅延計測（latency_measurement.h）の両方がこの駆動を使う。
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>

#include "vocroz/common/result.h"
#include "vocroz/netsim/fault_profile.h"
#include "vocroz/netsim/simulated_network.h"
#include "vocroz/test_harness/media_receiver.h"
#include "vocroz/test_harness/media_sender.h"
#include "vocroz/test_harness/return_sender.h"
#include "vocroz/test_harness/secure_link.h"
#include "vocroz/test_harness/simulated_datagram_socket.h"
#include "vocroz/transport/opus_frame_encoder.h"
#include "vocroz/transport/playout_stream.h"

#include "fakes/fake_clock.h"

namespace vocroz::test_harness {

/// ストリームの向き。
enum class MediaDirection : std::uint8_t {
    kUplink,  ///< 子機のキャプチャから親機の出力まで（子機の送信と親機の受信の部品）
    kReturn,  ///< 親機のミックスから子機の出力まで（返送モニター。docs/adr/0036-return-monitor.md）
};

/// 1 ストリームの端から端までの構成。
struct MediaLinkConfig {
    /// ストリームの向き。
    MediaDirection direction = MediaDirection::kUplink;
    /// 送信側から受信側への方向の障害。
    netsim::FaultProfile faults;
    /// 送信側のエンコーダーの設定。
    transport::OpusEncoderConfig encoder;
    /// 受信側の再生の設定。
    transport::PlayoutConfig playout;
    /// 送信側のオーディオコールバックのブロック長（サンプル数）。上りは子機のキャプチャ、
    /// 返送は親機の出力（ミックスを作るコールバック）。
    std::size_t capture_block = 128;
    /// 受信側の出力のコールバックのブロック長（サンプル数）。
    std::size_t output_block = 128;
};

/// 仮想時刻で動く、子機 1 台と親機の 1 ストリーム。
/// スレッド安全性: 1 つのスレッドから使う。
class MediaLinkSimulation {
public:
    /// キャプチャの 1 ブロックを埋める関数。first_sample
    /// はブロックの先頭のサンプル位置（キャプチャ時刻）。
    using Source = std::function<void(std::uint64_t first_sample, std::span<float> block)>;
    /// 出力の 1 ブロックを受け取る関数。first_sample はブロックの先頭のサンプル位置（再生時刻）。
    using Sink = std::function<void(std::uint64_t first_sample, std::span<const float> block)>;

    /// 子機のアドレス。
    static constexpr netsim::NodeAddress kClientAddress{.host = 2, .port = 50000};
    /// 親機のアドレス。
    static constexpr netsim::NodeAddress kHostAddress{.host = 1, .port = 40000};

    /// 構成を検証し、接続を確立し、送信ミュート（返送ならそのミュート）を解除した状態の
    /// シミュレーションを作る。
    /// @param config 構成
    /// @return 作ったシミュレーション。失敗したら原因のメッセージ
    [[nodiscard]] static common::Result<std::unique_ptr<MediaLinkSimulation>, std::string> create(
        const MediaLinkConfig& config);

    MediaLinkSimulation(const MediaLinkSimulation&) = delete;
    MediaLinkSimulation& operator=(const MediaLinkSimulation&) = delete;
    MediaLinkSimulation(MediaLinkSimulation&&) = delete;
    MediaLinkSimulation& operator=(MediaLinkSimulation&&) = delete;
    ~MediaLinkSimulation();

    /// 仮想時刻を samples だけ進める。
    /// 各時刻で、キャプチャ（ブロックの終わりに送信側へ渡す）→ ネットワークの配送 → 信号状態の更新
    /// → 出力（ブロックの始めに受信側から取り出す）の順に処理する。
    /// @param samples 進めるサンプル数
    /// @param source  キャプチャのブロックを埋める関数
    /// @param sink    出力のブロックを受け取る関数
    void run(std::uint64_t samples, const Source& source, const Sink& sink);

    /// 経路の障害や途絶を操作するためのネットワーク。
    [[nodiscard]] netsim::SimulatedNetwork& network() noexcept {
        return network_;
    }

    /// 送信側のアドレス。
    [[nodiscard]] netsim::NodeAddress sender_address() const noexcept;

    /// 受信側のアドレス。
    [[nodiscard]] netsim::NodeAddress receiver_address() const noexcept;

    /// 送信側のエンコーダーの先読みのサンプル数。遅延の見積もりに使う。
    [[nodiscard]] std::size_t codec_lookahead_samples() const noexcept;

    /// 受信側。返送では、子機の受信（HostReceivePath）と同じく、ミュートの世代番号で古いフレームを
    /// 捨ててから PlayoutStream へ入れる部品として使う。
    [[nodiscard]] const MediaReceiver& receiver() const noexcept {
        return *receiver_;
    }

    /// 今の仮想時刻（開始からのサンプル数）。
    [[nodiscard]] std::uint64_t now_samples() const noexcept {
        return now_samples_;
    }

    /// 構成。
    [[nodiscard]] const MediaLinkConfig& config() const noexcept {
        return config_;
    }

    /// create() からだけ呼ぶ。
    MediaLinkSimulation(const MediaLinkConfig& config, SecureLink link);

private:
    void advance_clock_to(std::uint64_t samples);

    MediaLinkConfig config_;
    common::FakeClock clock_;
    netsim::SimulatedNetwork network_{clock_};
    SecureLink link_;
    SimulatedDatagramSocket client_socket_{network_, kClientAddress, kHostAddress};
    SimulatedDatagramSocket host_socket_{network_, kHostAddress, kClientAddress};
    // 向きに応じて、どちらか一方だけを作る。
    std::unique_ptr<MediaSender> sender_;
    std::unique_ptr<ReturnSender> return_sender_;
    std::unique_ptr<MediaReceiver> receiver_;
    std::uint64_t now_samples_ = 0;
    std::size_t step_samples_ = 1;
    std::vector<float> capture_buffer_;
    std::vector<float> output_buffer_;
};

}  // namespace vocroz::test_harness
