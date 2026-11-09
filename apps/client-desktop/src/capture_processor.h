// SPDX-License-Identifier: AGPL-3.0-only
//
// 子機の送信側のオーディオスレッドの処理。入力（マイクかアプリ音声）1 つにつき 1 つ作る。
// キャプチャしたブロックに送信レベルと送信ミュートをゲインランプで掛け、1 フレーム分たまるごとに
// Opus（モノラルかステレオ）でエンコードし、入力のストリーム ID を付けて送信スレッドへ渡す。
// 送信レベル、送信ミュート、ミュートの世代番号はコントロールスレッドから SPSC キューで受け取り
// （AGENTS.md 6.1）、入力のピークはアトミック変数でコントロールスレッドへ返す。
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "vocroz/audio_core/gain_ramp.h"
#include "vocroz/audio_core/ports/audio_capture_callback.h"
#include "vocroz/common/spsc_queue.h"
#include "vocroz/transport/opus_frame_encoder.h"

#include "client_constants.h"
#include "encoded_frame.h"
#include "packet_sender.h"

namespace vocroz::client {

/// オーディオスレッドへ渡す送信の設定。
struct SendParams {
    /// 送信ミュート。初期ミュートの製品要件により true から始める。
    bool muted = true;
    /// 送信ミュートの世代番号。
    std::uint32_t mute_epoch = 0;
    /// 送信レベル（リニアゲイン）。
    float gain = 1.0F;
};

/// 送信側のオーディオスレッドの処理。
class CaptureProcessor final : public audio_core::AudioCaptureCallback {
public:
    /// @param encoder   エンコーダー。frame_samples() は kMaxFrameSamples 以下、channels() は
    ///                  transport::kMaxStreamChannels 以下
    /// @param sender    エンコード済みのフレームの渡し先。このオブジェクトより長く生存させる
    /// @param lane      送信スレッドへの経路（kCaptureLaneCount 未満）。同時に動く CaptureProcessor
    ///                  どうしで重ならないようにする
    /// @param stream_id フレームに付けるストリーム ID
    /// スレッド安全性: コントロールスレッドで生成する（作業領域をここで確保する）。
    CaptureProcessor(transport::OpusFrameEncoder encoder, PacketSender& sender, std::size_t lane,
                     std::uint16_t stream_id) noexcept;

    /// エンコードするチャンネル数（1 か 2）を返す。
    [[nodiscard]] std::size_t channels() const noexcept {
        return encoder_.channels();
    }

    /// フレームに付けるストリーム ID を返す。
    [[nodiscard]] std::uint16_t stream_id() const noexcept {
        return stream_id_;
    }

    /// 送信の設定を変える。次のブロックから、ゲインランプで反映する。
    /// @param params 新しい設定
    /// @return 渡せたら true。オーディオスレッドが止まっていて溜まりきったら false
    /// スレッド安全性: コントロールスレッド（生産者は 1 つ）から呼ぶ。
    [[nodiscard]] bool set_params(const SendParams& params) noexcept;

    /// キャプチャが止まっている間に、溜まった設定を捨てて params から始め直す。
    /// 入力なしの間はオーディオスレッドが設定を取り出さないので、キューが溜まりきって最新の設定
    /// （ミュートを含む）が落ちていることがある。新しいデバイスで始める前に呼び、確実に最新の設定で
    /// 始める。ゲインは 0 からランプで上げ、開始の瞬間のクリックノイズを避ける。
    /// @param params 最新の設定
    /// スレッド安全性: コントロールスレッドから、AudioInput::start() の前か stop() の後にだけ呼ぶ
    /// （オーディオスレッドが on_captured() を呼んでいない間は、キューの消費者を引き継げる）。
    void restart(const SendParams& params) noexcept;

    /// 前回の呼び出しから今回までの入力のピーク（リニア値、送信レベルを掛ける前）を返し、0 に戻す。
    /// 送信ミュート中も入力を測る（出演者がマイクの入りを確かめられるようにするため）。
    /// スレッド安全性: コントロールスレッドから呼ぶ。
    [[nodiscard]] float take_input_peak() noexcept;

    /// キャプチャしたブロックを処理する。モノラルでエンコードするときは入力の 1 チャンネル目を、
    /// ステレオのときは 1、2 チャンネル目を使う。ステレオでエンコードするのにモノラルの入力しか
    /// なければ、左右に同じ信号を入れる（チャンネル数を選び間違えても無音にしない）。
    /// エラー時の振る舞い: エンコードに失敗したフレームと、送信待ちが満杯で入らなかったフレームは
    /// 捨てる。タイムスタンプは進めるので、受信側からは欠損と同じに見える。
    /// スレッド安全性: オーディオスレッドから呼び出し可能。
    void on_captured(audio_core::ConstInterleavedBuffer in) noexcept VOCROZ_NONBLOCKING override;

    /// 捨てたフレームの数を返す（エンコードの失敗と、送信待ちの満杯）。
    /// スレッド安全性: どのスレッドからも呼び出し可能。
    [[nodiscard]] std::uint64_t dropped_frames() const noexcept {
        return dropped_frames_.load(std::memory_order_relaxed);
    }

private:
    // 設定の変更はフェーダーの操作の間に数十回届く程度なので、この数で溢れない。
    static constexpr std::size_t kParamQueueCapacity = 64;

    void apply_pending_params() noexcept VOCROZ_NONBLOCKING;
    void finish_frame() noexcept VOCROZ_NONBLOCKING;

    transport::OpusFrameEncoder encoder_;
    PacketSender& sender_;
    const std::size_t lane_;
    const std::uint16_t stream_id_;
    common::SpscQueue<SendParams, kParamQueueCapacity> params_;
    std::atomic<float> input_peak_{0.0F};
    std::atomic<std::uint64_t> dropped_frames_{0};

    // 以下はオーディオスレッドだけが読み書きする。
    audio_core::GainRamp ramp_{kSendGainRampSamples};
    SendParams current_;
    // インターリーブの PCM。filled_ はチャンネルあたりのサンプル数。
    std::array<float, kMaxFrameSamples * transport::kMaxStreamChannels> frame_{};
    std::size_t filled_ = 0;
    std::uint64_t next_timestamp_ = 0;
    EncodedFrame encoded_;
};

}  // namespace vocroz::client
