// SPDX-License-Identifier: AGPL-3.0-only
//
// Opus のエンコーダーとデコーダーが返すエラーの原因。
// libopus のエラーコードをそのまま外に出さず、呼び出し側が判断に使う粒度にまとめる。
#pragma once

#include <cstdint>

namespace vocroz::transport {

/// Opus の処理に失敗した原因。
enum class CodecError : std::uint8_t {
    kCreateFailed,      ///< エンコーダーかデコーダーの生成か設定に失敗した
    kInvalidFrameSize,  ///< 渡された PCM の長さが 1 フレームの長さと違う
    kBufferTooSmall,    ///< 出力先の領域が足りない
    kEncodeFailed,      ///< エンコードに失敗した
    kInvalidPacket,     ///< パケットが壊れているか、想定外のフレーム長
};

}  // namespace vocroz::transport
