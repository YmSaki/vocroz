// SPDX-License-Identifier: AGPL-3.0-only
//
// 固定ヘッダーの書き込み。読み込み（parse_packet_header()）は公開ヘッダーで宣言し、
// 同じ packet_header_codec.cpp で実装して、読み書きのバイト配置を 1 か所にまとめる。
#pragma once

#include <cstdint>
#include <span>

#include "vocroz/transport/packet_header.h"

namespace vocroz::transport::wire {

/// header を out の先頭 kPacketHeaderBytes バイトに書く。
/// 前提条件: out.size() >= kPacketHeaderBytes。呼び出し側で長さを確かめてから呼ぶ。
/// スレッド安全性: 純粋関数で、オーディオスレッドから呼び出し可能。
void write_packet_header(const PacketHeader& header, std::span<std::uint8_t> out) noexcept;

}  // namespace vocroz::transport::wire
