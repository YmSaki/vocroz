// SPDX-License-Identifier: AGPL-3.0-only
//
// session の CaptureControl の実装。子機の入力（マイクとアプリ音声）ごとに、デバイスを開いて
// キャプチャを始め、送信ミュートと送信レベルをそれぞれのオーディオスレッドの CaptureProcessor へ
// 渡す（docs/adr/0035-client-input-expansion.md）。
// 入力の構成（チャンネル数、ストリーム ID）が変わったら、CaptureProcessor とエンコーダーを
// 作り直す。エンコーダーのチャンネル数は作るときに決まり、オーディオスレッドでは変えられないため。
// デバイスの抜去はデバイスのスレッドから届くので、アトミック変数に記録してコントロールスレッドで
// 取り出す（ClientRuntime が一覧を取り直して session に知らせる）。
#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "vocroz/audio_core/ports/audio_device_listener.h"
#include "vocroz/audio_core/ports/audio_input.h"
#include "vocroz/common/ports/logger.h"
#include "vocroz/session/input_stream.h"
#include "vocroz/session/ports/capture_control.h"
#include "vocroz/transport/opus_frame_encoder.h"

#include "capture_processor.h"
#include "client_constants.h"
#include "input_device_opener.h"
#include "packet_sender.h"

namespace vocroz::client {

/// 子機のキャプチャの操作。
/// スレッド安全性: CaptureControl のメンバー関数、flush_params()、take_device_lost()、
/// take_input_peak() はコントロールスレッドから呼ぶ。on_device_lost() はデバイスのスレッドから
/// 呼ばれる。
class ClientCapture final : public session::CaptureControl {
public:
    /// 注入するもの。どれもこのオブジェクトより長く生存させる。
    struct Ports {
        /// エンコード済みのフレームの渡し先。
        PacketSender& sender;
        /// マイクの入力デバイスを ID で開く。
        InputDeviceOpener& microphones;
        /// アプリ音声（再生デバイスのループバック）を開く。ID は空文字列（既定の再生デバイス）。
        InputDeviceOpener& app_audio;
        /// デバイスやエンコーダーを開けなかったときの出力先。
        common::Logger& logger;
    };

    /// 申告前の構成（ストリーム 0 のモノラルのマイク）で始める。
    /// @param ports   注入するもの
    /// @param encoder エンコーダーの設定。チャンネル数とビットレートは入力ごとに上書きする
    /// @param mic モノラルのマイクのエンコーダー（生成の失敗を起動時に返すため、呼び出し側が作る）
    ClientCapture(Ports ports, const transport::OpusEncoderConfig& encoder,
                  transport::OpusFrameEncoder mic);

    /// キャプチャを止めてから破棄する。
    ~ClientCapture() override;

    ClientCapture(const ClientCapture&) = delete;
    ClientCapture& operator=(const ClientCapture&) = delete;

    /// マイクのデバイスを開き直してキャプチャを始める。開けなければ入力なしのままにする
    /// （別のデバイスへは切り替えない）。開けなかったデバイスがまだ一覧にあれば、出演者は
    /// 選び直せる。
    void select_input(const std::optional<std::string>& device_id) override;

    void set_send_muted(bool muted, std::uint32_t epoch) override;

    /// 送信レベルを反映する。範囲の検証は session が済ませている。
    void set_send_level(double level_db) override;

    /// 入力の構成を反映する。変わった入力だけを作り直し、変わらない入力のキャプチャは止めない。
    /// エンコーダーを作れなかった入力は送らない（ログに残す）。
    void set_streams(const std::vector<session::InputStream>& streams) override;

    /// 渡せなかった送信の設定を、オーディオスレッドへ渡し直す。アプリ音声のデバイスが抜けて
    /// いたら、一度だけ開き直す（既定の再生デバイスが切り替わった場合に追従するため）。
    /// コントロールスレッドの周期ごとに呼ぶ。
    /// オーディオスレッドが一時的に止まってキューが溜まりきると、最新の設定（ミュートを含む）が
    /// 落ちる。古い設定のまま再開しないよう、届くまで渡し直す。
    void flush_params();

    /// マイクのデバイスが抜去されたら、その後に 1 回だけ true を返す。
    [[nodiscard]] bool take_device_lost() noexcept;

    /// 前回の呼び出しからのマイクの入力のピーク（リニア値）を返し、0 に戻す。マイクを送って
    /// いなければ 0 を返す。
    [[nodiscard]] float take_input_peak() noexcept;

    /// マイクをキャプチャ中なら true を返す。
    [[nodiscard]] bool capturing() const noexcept {
        return lanes_[kMicrophoneLane].input != nullptr;
    }

    /// アプリ音声をキャプチャ中なら true を返す。
    [[nodiscard]] bool capturing_app_audio() const noexcept {
        return lanes_[kAppAudioLane].input != nullptr;
    }

    /// 入力の CaptureProcessor を返す。その入力を送らない構成なら nullptr。テストと診断のため。
    [[nodiscard]] const CaptureProcessor* processor(std::size_t lane) const noexcept {
        return lanes_[lane].processor.get();
    }

private:
    // 入力 1 つ分のキャプチャ。デバイスの抜去は経路ごとに記録する。
    struct Lane final : audio_core::AudioDeviceListener {
        void on_device_lost() noexcept override {
            device_lost.store(true, std::memory_order_release);
        }

        std::optional<session::InputStream> stream;
        std::unique_ptr<CaptureProcessor> processor;
        std::unique_ptr<audio_core::AudioInput> input;
        bool params_pending = false;
        std::atomic<bool> device_lost{false};
    };

    void stop(Lane& lane) noexcept;
    void open(std::size_t lane_index);
    void reconfigure(std::size_t lane_index, const std::optional<session::InputStream>& stream);
    void push_params();

    Ports ports_;
    transport::OpusEncoderConfig encoder_config_;
    std::optional<std::string> mic_device_;
    SendParams params_;
    std::array<Lane, kCaptureLaneCount> lanes_;
};

}  // namespace vocroz::client
