// SPDX-License-Identifier: AGPL-3.0-only
//
// 子機の送信側で、1 フレームの PCM を Opus のペイロードにエンコードする。
// 48kHz モノラル、OPUS_APPLICATION_RESTRICTED_LOWDELAY、DTX なしの設定を 1 か所で固定する
// （docs/adr/0002-opus-low-delay.md）。libopus の型は公開ヘッダーに出さない。
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include "vocroz/common/result.h"
#include "vocroz/transport/codec_error.h"
#include "vocroz/transport/constants.h"

// libopus の型。定義は opus.h にあり、ここでは名前だけを宣言してポインターとして持つ。
struct OpusEncoder;

namespace vocroz::transport {

/// エンコーダーの設定。
struct OpusEncoderConfig {
    /// 1 フレームの長さ。
    FrameDuration frame_duration = FrameDuration::k5000Us;
    /// 目標ビットレート（bps）。最終値は P0 の実測で決める。
    /// 2.5〜5ms の短いフレームでは CELT のフレームあたりのオーバーヘッドが大きいので、
    /// 楽器の音質を保てるよう高めの値を既定にする。
    std::int32_t bitrate_bps = 128000;
};

/// 1 フレームずつエンコードする Opus のエンコーダー。
/// スレッド安全性: 1 つのオブジェクトを同時に 1 スレッドからだけ使う。
/// create() はヒープを確保するのでオーディオスレッド外で呼ぶ。encode()
/// はオーディオスレッドから呼び出し可能。
class OpusFrameEncoder {
public:
    /// エンコーダーを生成して設定する。
    /// @param config 設定
    /// @return 生成したエンコーダー。libopus が生成か設定を拒否したら CodecError::kCreateFailed
    [[nodiscard]] static common::Result<OpusFrameEncoder, CodecError> create(
        const OpusEncoderConfig& config) noexcept;

    OpusFrameEncoder(OpusFrameEncoder&&) noexcept;
    OpusFrameEncoder& operator=(OpusFrameEncoder&&) noexcept;
    OpusFrameEncoder(const OpusFrameEncoder&) = delete;
    OpusFrameEncoder& operator=(const OpusFrameEncoder&) = delete;
    ~OpusFrameEncoder();

    /// 1 フレームの PCM をエンコードする。
    /// @param pcm 1 フレーム分（frame_samples() 個）の Float32 PCM。範囲は -1.0〜1.0
    /// @param out ペイロードの書き込み先。kMaxOpusPayloadBytes を超える部分は使わない
    /// @return 書き込んだバイト数。pcm の長さが違えば kInvalidFrameSize、エンコードに失敗したら
    /// kEncodeFailed スレッド安全性:
    /// オーディオスレッドから呼び出し可能（ヒープを確保せず、ブロックしない）。
    [[nodiscard]] common::Result<std::size_t, CodecError> encode(
        std::span<const float> pcm, std::span<std::uint8_t> out) noexcept;

    /// 1 フレームのサンプル数を返す。
    [[nodiscard]] std::size_t frame_samples() const noexcept {
        return frame_samples_;
    }

    /// エンコーダーのアルゴリズム遅延（先読み）をサンプル数で返す。遅延の見積もりに使う。
    [[nodiscard]] std::size_t lookahead_samples() const noexcept {
        return lookahead_samples_;
    }

private:
    // libopus のエンコーダーを破棄する。opus.h を公開ヘッダーに含めないよう、定義は .cpp に置く。
    struct Deleter {
        void operator()(::OpusEncoder* encoder) const noexcept;
    };
    using Handle = std::unique_ptr<::OpusEncoder, Deleter>;

    OpusFrameEncoder(Handle encoder, std::size_t frame_samples,
                     std::size_t lookahead_samples) noexcept;

    Handle encoder_;
    std::size_t frame_samples_;
    std::size_t lookahead_samples_;
};

}  // namespace vocroz::transport
