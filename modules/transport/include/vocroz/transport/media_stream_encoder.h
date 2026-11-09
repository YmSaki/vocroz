// SPDX-License-Identifier: AGPL-3.0-only
//
// 送信側のオーディオスレッドの処理。受け取ったサンプルに送信レベルとミュートを
// ゲインランプで掛け、1 フレームぶん溜まるごとに Opus でエンコードして MediaFrameSink へ渡す。
// 親機から子機への返送（下りストリーム、モノラル。docs/adr/0036-return-monitor.md）と、
// SRTP 外部出力（モノラルかステレオ。docs/adr/0040-srtp-external-output.md）で使う。
// 入力のチャンネル数とエンコーダーのチャンネル数が違うときは、ここでそろえる
// （モノラルの入力をステレオで送るなら L と R に同じ音を入れ、ステレオの入力をモノラルで
// 送るなら L と R の平均を取る）。
//
// ミュートの規則（方向別のデータフロー規則）:
// - ミュートしたら、ランプで無音にしてから送信を止める。止めている間はエンコードもしない。
// - 止めている間もタイムスタンプは実時間どおりに進める（受信側は飛びを無音の区間として扱える）。
// - 各フレームには送信時点の世代番号（epoch）を載せ、受信側が停止前の古いフレームを
//   区別できるようにする。
// 設定はコントロールスレッドから SPSC キューで受け取る（AGENTS.md 6.1）。
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

#include "vocroz/audio_core/gain_ramp.h"
#include "vocroz/audio_core/ports/audio_sink.h"
#include "vocroz/audio_core/ports/audio_stream_sink.h"
#include "vocroz/audio_core/realtime.h"
#include "vocroz/common/spsc_queue.h"
#include "vocroz/transport/constants.h"
#include "vocroz/transport/media_frame.h"
#include "vocroz/transport/opus_frame_encoder.h"
#include "vocroz/transport/ports/media_frame_sink.h"

namespace vocroz::transport {

/// ミュートとミュート解除のランプの長さ（サンプル）。5ms。
/// 送信を止める前にフェードアウトし、クリックノイズを防ぐ（5〜20ms のゲインランプ）。
inline constexpr std::size_t kStreamMuteRampSamples = 240;

/// オーディオスレッドへ渡す送信の設定。
struct StreamSendParams {
    /// ミュート。初期ミュートの製品要件により true から始める。
    bool muted = true;
    /// ミュートの世代番号。ミュートを解除するたびに 1 増やした値を渡す。
    std::uint32_t mute_epoch = 0;
    /// 送信レベル（リニアゲイン）。
    float gain = 1.0F;
};

/// 1 本のストリームのエンコードと送信の判断。
/// スレッド安全性: 生成と set_params() はコントロールスレッド、push() と push_stereo() は
/// オーディオスレッド。1 つのオブジェクトには push() か push_stereo() のどちらか一方だけを使う。
class MediaStreamEncoder final : public audio_core::AudioSink, public audio_core::AudioStreamSink {
public:
    /// @param encoder エンコーダー
    /// @param sink    エンコード済みのフレームの渡し先。このオブジェクトより長く生存させる
    /// スレッド安全性: コントロールスレッドで生成する。
    MediaStreamEncoder(OpusFrameEncoder encoder, MediaFrameSink& sink) noexcept;

    /// 送信の設定を変える。次のブロックから、ゲインランプで反映する。
    /// @param params 新しい設定
    /// @return 渡せたら true。オーディオスレッドが止まっていて溜まりきったら false
    /// スレッド安全性: コントロールスレッド（生産者は 1 つ）から呼ぶ。
    [[nodiscard]] bool set_params(const StreamSendParams& params) noexcept;

    /// 1 ブロックのサンプルを受け取る（AudioSink）。
    /// エラー時の振る舞い: エンコードに失敗したフレームと、送信待ちが満杯で入らなかったフレームは
    /// 捨てて dropped_frames() に数える。タイムスタンプは進めるので、受信側からは欠損と同じに
    /// 見える。
    /// スレッド安全性: オーディオスレッドから呼び出し可能。
    void push(std::span<const float> samples) noexcept VOCROZ_NONBLOCKING override;

    /// 1 ブロックのステレオのサンプルを受け取る（AudioStreamSink）。
    /// エラー時の振る舞いは push() と同じ。left と right の短い方の長さだけを使う。
    /// スレッド安全性: オーディオスレッドから呼び出し可能。
    void push_stereo(std::span<const float> left,
                     std::span<const float> right) noexcept VOCROZ_NONBLOCKING override;

    /// 捨てたフレームの数を返す（エンコードの失敗と、送信待ちの満杯）。
    /// スレッド安全性: どのスレッドからも呼び出し可能。
    [[nodiscard]] std::uint64_t dropped_frames() const noexcept {
        return dropped_frames_.load(std::memory_order_relaxed);
    }

private:
    // 設定の変更はオペレーターの操作の間に数回届く程度なので、この数で溢れない。
    static constexpr std::size_t kParamQueueCapacity = 64;
    // 5ms のステレオ（2 チャンネル）が最大。
    static constexpr std::size_t kMaxFrameSamples = frame_samples(FrameDuration::k5000Us) * 2;

    void apply_pending_params() noexcept VOCROZ_NONBLOCKING;
    void append(float left, float right) noexcept VOCROZ_NONBLOCKING;
    void finish_frame() noexcept VOCROZ_NONBLOCKING;

    OpusFrameEncoder encoder_;
    MediaFrameSink& sink_;
    common::SpscQueue<StreamSendParams, kParamQueueCapacity> params_;
    std::atomic<std::uint64_t> dropped_frames_{0};

    // 以下はオーディオスレッドだけが読み書きする。
    audio_core::GainRamp ramp_{kStreamMuteRampSamples};
    StreamSendParams current_;
    std::array<float, kMaxFrameSamples> frame_{};
    std::size_t filled_ = 0;  // フレームに溜めたサンプル時刻の数（チャンネル数を掛けない）
    std::uint64_t next_timestamp_ = 0;
    MediaFrame encoded_;
};

}  // namespace vocroz::transport
