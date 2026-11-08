// SPDX-License-Identifier: AGPL-3.0-only
//
// 親機の受信側で、1 フレームの Opus のペイロードを PCM にデコードし、
// 欠損したフレームを PLC で補う。
// 低遅延モードではインバンド FEC が使えないので、欠損の補間は PLC
// だけで行う（docs/adr/0002-opus-low-delay.md）。
// libopus の型は公開ヘッダーに出さない。
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include "vocroz/audio_core/realtime.h"
#include "vocroz/common/result.h"
#include "vocroz/transport/codec_error.h"

// libopus の型。
// 定義は opus.h にあり、ここでは名前だけを宣言してポインターとして持つ。
struct OpusDecoder;

namespace vocroz::transport {

/// 48kHz モノラルの Opus のデコーダー。
/// スレッド安全性: 1 つのオブジェクトを同時に 1 スレッドからだけ使う。
/// create() はヒープを確保するのでオーディオスレッド外で呼ぶ。
/// それ以外はオーディオスレッドから呼び出し可能。
class OpusFrameDecoder {
public:
    /// デコーダーを生成する。
    /// @return 生成したデコーダー。libopus が生成を拒否したら CodecError::kCreateFailed
    [[nodiscard]] static common::Result<OpusFrameDecoder, CodecError> create() noexcept;

    OpusFrameDecoder(OpusFrameDecoder&&) noexcept;
    OpusFrameDecoder& operator=(OpusFrameDecoder&&) noexcept;
    OpusFrameDecoder(const OpusFrameDecoder&) = delete;
    OpusFrameDecoder& operator=(const OpusFrameDecoder&) = delete;
    ~OpusFrameDecoder();

    /// 1 フレームをデコードする。
    /// @param payload Opus のペイロード
    /// @param out     PCM の書き込み先。長さはフレーム長以上にする
    /// @return 書き込んだサンプル数。壊れたパケットや out に収まらないフレームは kInvalidPacket
    /// スレッド安全性: オーディオスレッドから呼び出し可能（ヒープを確保せず、ブロックしない）。
    [[nodiscard]] common::Result<std::size_t, CodecError> decode(
        std::span<const std::uint8_t> payload, std::span<float> out) noexcept VOCROZ_NONBLOCKING;

    /// 欠損した 1 フレームを PLC で補間する。
    /// @param out 補間した PCM の書き込み先。長さが補間するサンプル数になる（2.5ms の整数倍）
    /// @return 書き込んだサンプル数。長さが 2.5ms の整数倍でなければ kInvalidFrameSize
    /// スレッド安全性: オーディオスレッドから呼び出し可能。
    [[nodiscard]] common::Result<std::size_t, CodecError> conceal(std::span<float> out) noexcept
        VOCROZ_NONBLOCKING;

    /// 内部状態を初期化する。
    /// 前の区間の音を次の区間に持ち込まないために、区間の切れ目で呼ぶ。
    /// スレッド安全性: オーディオスレッドから呼び出し可能。
    void reset() noexcept VOCROZ_NONBLOCKING;

private:
    // libopus のデコーダーを破棄する。
    // opus.h を公開ヘッダーに含めないよう、定義は .cpp に置く。
    struct Deleter {
        void operator()(::OpusDecoder* decoder) const noexcept;
    };
    using Handle = std::unique_ptr<::OpusDecoder, Deleter>;

    explicit OpusFrameDecoder(Handle decoder) noexcept;

    Handle decoder_;
};

}  // namespace vocroz::transport
