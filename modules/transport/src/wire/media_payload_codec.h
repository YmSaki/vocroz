// SPDX-License-Identifier: AGPL-3.0-only
//
// メディアパケットの平文（MediaPayload）の書き込み。読み込み（decode_media_payload()）は
// 公開ヘッダーで宣言し、同じ media_payload_codec.cpp で実装して、バイト配置を 1 か所にまとめる。
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "vocroz/transport/media_payload.h"

namespace vocroz::transport::wire {

/// payload を平文として書いたときのバイト数を返す。
/// @param payload 書く値
[[nodiscard]] constexpr std::size_t media_payload_bytes(const MediaPayload& payload) noexcept {
    return kMediaPayloadHeaderBytes + payload.opus_payload.size();
}

/// payload を out の先頭に書く。
/// 前提条件: out.size() >= media_payload_bytes(payload)。呼び出し側で長さを確かめてから呼ぶ。
/// @return 書いたバイト数
/// スレッド安全性: 純粋関数で、オーディオスレッドから呼び出し可能。
std::size_t write_media_payload(const MediaPayload& payload, std::span<std::uint8_t> out) noexcept;

}  // namespace vocroz::transport::wire
