// SPDX-License-Identifier: AGPL-3.0-only
//
// 受信側のパイプライン（ジッターバッファ、デコーダー）に渡す、検証済みの 1 フレームの値型。
// ワイヤーフォーマットのパース（src/wire/）が復号済みのパケットからこの型を作る。
// ネットワーク受信スレッドからオーディオスレッドへ SPSC キューで渡すため、
// ヒープを持たないトリビアルにコピー可能な型にしている。
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "vocroz/audio_core/realtime.h"
#include "vocroz/common/result.h"
#include "vocroz/transport/constants.h"

namespace vocroz::transport {

/// メディアフレームを作れなかった原因。
enum class MediaFrameError : std::uint8_t {
    kEmptyPayload,  ///< ペイロードが空。Opus の空パケットは DTX を意味するが、DTX は使わない
    kPayloadTooLarge,  ///< ペイロードが kMaxOpusPayloadBytes を超えている
};

/// 1 ストリームの 1 フレーム。
/// スレッド安全性: 通常の値型と同じ。
struct MediaFrame {
    /// フレームの先頭サンプルのメディアタイムスタンプ（48kHz のサンプル数）。
    /// 再生順序は到着時刻ではなくこの値で決める。
    /// 1 ストリーム内ではフレーム長の整数倍ずつ進む。
    std::uint64_t timestamp = 0;
    /// 送信時点のミュートの世代番号（epoch）。
    /// 送信ミュートを解除するたびに 1 増える。
    /// 制御メッセージとメディアフレームの到着順が逆転しても、
    /// 停止前の古いフレームを区別できるようにする。
    std::uint32_t mute_epoch = 0;
    /// payload のうち有効なバイト数。
    std::uint16_t payload_size = 0;
    /// Opus のペイロード。
    /// 先頭 payload_size バイトだけが有効。
    std::array<std::uint8_t, kMaxOpusPayloadBytes> payload{};

    /// 有効なペイロードを返す。
    /// @return 先頭 payload_size バイトを指す span。フレームの寿命の間だけ有効
    /// スレッド安全性: オーディオスレッドから呼び出し可能。
    [[nodiscard]] std::span<const std::uint8_t> payload_bytes() const noexcept VOCROZ_NONBLOCKING {
        return std::span<const std::uint8_t>{payload}.first(payload_size);
    }
};

/// 検証済みのメディアフレームを作る。
/// @param timestamp  フレームの先頭サンプルのメディアタイムスタンプ（48kHz のサンプル数）
/// @param mute_epoch 送信時点のミュートの世代番号
/// @param payload    Opus のペイロード。内容はコピーする
/// @return 作ったフレーム。ペイロードが空か長すぎれば MediaFrameError
/// スレッド安全性: どのスレッドからも呼び出し可能。ヒープを確保しない。
[[nodiscard]] common::Result<MediaFrame, MediaFrameError> make_media_frame(
    std::uint64_t timestamp, std::uint32_t mute_epoch,
    std::span<const std::uint8_t> payload) noexcept;

}  // namespace vocroz::transport
