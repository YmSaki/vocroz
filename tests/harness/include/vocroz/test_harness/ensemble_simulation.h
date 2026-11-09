// SPDX-License-Identifier: AGPL-3.0-only
//
// 「親機 + 子機 N 台」の結合テストの駆動。
// 仮想時刻のシミュレーションのネットワークの上に SimulatedHost と SimulatedClient を置き、
// キャプチャのコールバック、ネットワークの配送、各ノードの処理、出力のコールバックをサンプル単位で順に進める。
// ランデブーサーバーは、招待の登録と解決をその場で返す代わりの実装でつなぐ（実際のサーバーの試験は
// tests/nat）。
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "vocroz/common/result.h"
#include "vocroz/control_protocol/commands.h"
#include "vocroz/control_protocol/entities.h"
#include "vocroz/control_protocol/states.h"
#include "vocroz/netsim/fault_profile.h"
#include "vocroz/netsim/simulated_network.h"
#include "vocroz/test_harness/simulated_client.h"
#include "vocroz/test_harness/simulated_host.h"
#include "vocroz/transport/opus_frame_encoder.h"
#include "vocroz/transport/playout_stream.h"

#include "fakes/fake_clock.h"

namespace vocroz::test_harness {

/// 合奏のシミュレーションの設定。
struct EnsembleConfig {
    /// 子機の名前。子機の台数はこの数になる。
    std::vector<std::string> client_names;
    /// 子機ごとに承認と送信ミュートを無視する子機にするか（client_names と同じ並び。足りない分は
    /// false）。
    std::vector<bool> ignore_send_mute;
    /// 子機と親機の間の回線（両方向に同じ設定を使う）。
    netsim::FaultProfile link;
    /// エンコーダーの設定。
    transport::OpusEncoderConfig encoder;
    /// 親機の接続ごとの再生の設定。
    transport::PlayoutConfig playout;
    /// キャプチャと出力のコールバックのブロック長（サンプル数）。
    std::size_t block = 128;
};

/// 合奏のシミュレーション。
class EnsembleSimulation {
public:
    /// 子機 index の 1 ブロック分の入力を書き込む関数。first_sample
    /// はブロックの先頭のサンプル時刻。マイクをステレオにした子機と、アプリ音声には、L、R の
    /// インターリーブで 2 倍の長さのブロックを渡す。
    using Source =
        std::function<void(std::size_t client, std::uint64_t first_sample, std::span<float> block)>;
    /// 親機の Master の 1 ブロック分の出力を受け取る関数。first_sample は鳴らし始めるサンプル時刻。
    using Sink = std::function<void(std::uint64_t first_sample, std::span<const float> block)>;

    static constexpr netsim::NodeAddress kHostAddress{.host = 1, .port = 40000};

    /// @param config 設定
    explicit EnsembleSimulation(EnsembleConfig config);
    EnsembleSimulation(const EnsembleSimulation&) = delete;
    EnsembleSimulation& operator=(const EnsembleSimulation&) = delete;
    EnsembleSimulation(EnsembleSimulation&&) = delete;
    EnsembleSimulation& operator=(EnsembleSimulation&&) = delete;
    ~EnsembleSimulation();

    /// samples サンプル分の時間を進める。
    /// @param samples 進めるサンプル数
    /// @param source  子機の入力。空なら無音
    /// @param sink    親機の出力。空なら捨てる
    /// @param app_audio 子機のアプリ音声の入力（ステレオ）。空ならアプリ音声は送らない
    void run(std::uint64_t samples, const Source& source = {}, const Sink& sink = {},
             const Source& app_audio = {});

    /// 時間を進める（入力は無音、出力は捨てる）。
    /// @param duration 進める時間
    void run_for(std::chrono::milliseconds duration);

    /// 親機の UI から離散コマンドを送り、応答を返す。
    /// @param entity_id 対象のエンティティ
    /// @param body      コマンドの本体
    /// @return Ack なら適用後の revision、Reject なら原因。応答がなければ kEngineError
    common::Result<control_protocol::Revision, control_protocol::ErrorCode> host_command(
        const std::string& entity_id, control_protocol::DiscreteCommandBody body);

    /// 子機 index の UI から離散コマンドを送り、応答を返す。
    /// @param index 子機の番号
    /// @param body  コマンドの本体
    /// @return host_command() と同じ
    common::Result<control_protocol::Revision, control_protocol::ErrorCode> client_command(
        std::size_t index, control_protocol::DiscreteCommandBody body);

    /// 子機 index と親機の間の回線を、両方向とも途絶させる（届かないパケットは捨てる）。
    /// @param index 子機の番号
    void disconnect(std::size_t index);

    /// disconnect() した回線を戻す。
    /// @param index 子機の番号
    void restore(std::size_t index);

    /// 親機。
    [[nodiscard]] SimulatedHost& host() noexcept {
        return *host_;
    }

    /// 子機 index。
    [[nodiscard]] SimulatedClient& client(std::size_t index) noexcept {
        return *clients_.at(index);
    }

    /// 子機の台数。
    [[nodiscard]] std::size_t client_count() const noexcept {
        return clients_.size();
    }

    /// 親機に発行された招待の文字列（未発行なら std::nullopt）。
    [[nodiscard]] std::optional<std::string> invite() const;

    /// 経過したサンプル数。
    [[nodiscard]] std::uint64_t now_samples() const noexcept {
        return now_samples_;
    }

    /// ネットワーク。
    [[nodiscard]] netsim::SimulatedNetwork& network() noexcept {
        return network_;
    }

private:
    void advance_clock_to(std::uint64_t samples);
    void serve_rendezvous();

    EnsembleConfig config_;
    common::FakeClock clock_;
    netsim::SimulatedNetwork network_{clock_};
    std::unique_ptr<SimulatedHost> host_;
    std::vector<std::unique_ptr<SimulatedClient>> clients_;
    std::size_t served_registrations_ = 0;
    std::vector<std::size_t> served_resolutions_;
    std::uint64_t now_samples_ = 0;
    std::size_t step_samples_ = 1;
    std::vector<float> capture_buffer_;
    std::vector<float> output_buffer_;
    int request_number_ = 0;
};

}  // namespace vocroz::test_harness
