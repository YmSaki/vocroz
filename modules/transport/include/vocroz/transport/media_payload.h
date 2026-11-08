// SPDX-License-Identifier: AGPL-3.0-only
//
// メディアパケットの暗号文側に載せる中身。
// ストリーム ID、48kHz のタイムスタンプ、ミュート epoch、Opus のペイロードを暗号化して運び、
// 経路上の第三者からはどのストリームのどの時刻の音声かも見えないようにする。
// 送信は PacketSealer::seal_media() で行う。
// 受信は PacketOpener::open() で復号してから decode_media_payload() で変換する。
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "vocroz/common/result.h"
#include "vocroz/transport/transport_error.h"

namespace vocroz::transport {

/// メディアパケットの中身。opus_payload は受信バッファか送信元のバッファを指すだけで、所有しない。
struct MediaPayload {
    /// 接続内のストリームの識別子（マイク入力など）。
    std::uint16_t stream_id;
    /// フレームの先頭のタイムスタンプ。入力デバイスのレートによらず 48kHz のサンプル数で進める。
    /// 64 ビットにして、長時間のセッションでも折り返しを考えずに済むようにする。
    std::uint64_t timestamp;
    /// 送信ミュートの世代番号。送信ミュートを解除するたびに 1 増える。
    /// 受信側は、ミュート通知より古い世代のフレームを捨てるのに使う。
    std::uint32_t mute_epoch;
    /// Opus のペイロード（1 フレーム）。DTX は使わないので、空は不正として扱う
    /// （docs/adr/0002-opus-low-delay.md）。
    std::span<const std::uint8_t> opus_payload;
};

/// MediaPayload の固定部（ストリーム ID 2、タイムスタンプ 8、ミュート epoch 4）の長さ（バイト）。
inline constexpr std::size_t kMediaPayloadHeaderBytes = 14;

/// 復号済みのメディアパケットの平文を MediaPayload に変換する。
/// @param plaintext PacketOpener::open() が返した平文
/// @return 変換した値。opus_payload は plaintext の一部を指す。固定部より短いか、
///         Opus のペイロードが空なら kMalformedPayload
/// スレッド安全性: 純粋関数で、どのスレッドからも呼び出し可能。オーディオスレッドから呼び出し可能。
[[nodiscard]] common::Result<MediaPayload, TransportError> decode_media_payload(
    std::span<const std::uint8_t> plaintext) noexcept;

}  // namespace vocroz::transport
