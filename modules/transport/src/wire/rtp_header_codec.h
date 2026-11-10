// SPDX-License-Identifier: AGPL-3.0-only
//
// SRTP 外部出力の RTP 固定ヘッダー（RFC 3550）の書き込み。
// 親機と子機の間の独自のパケットヘッダー（packet_header_codec）とは別の形式で、互いに使わない
// （docs/adr/0040-srtp-external-output.md）。CSRC とヘッダー拡張は使わないので、ヘッダーは常に 12
// バイト。
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace vocroz::transport::wire {

/// RTP 固定ヘッダーの長さ（バイト）。CSRC とヘッダー拡張を使わないときの長さ。
inline constexpr std::size_t kRtpHeaderBytes = 12;

/// RTP 固定ヘッダーの値。
struct RtpHeader {
    bool marker = false;  ///< マーカービット。送信の区切りの最初のパケットで立てる
    std::uint8_t payload_type = 0;  ///< ペイロードタイプ（0〜127）
    std::uint16_t sequence = 0;     ///< シーケンス番号
    std::uint32_t timestamp = 0;    ///< RTP タイムスタンプ（Opus は 48kHz の時計）
    std::uint32_t ssrc = 0;         ///< 同期送信元の識別子
};

/// header を out の先頭 kRtpHeaderBytes バイトに書く。バージョンは 2、パディング、拡張、CSRC
/// は使わない。 前提条件: out.size() >= kRtpHeaderBytes、header.payload_type <=
/// 127。呼び出し側で確かめてから呼ぶ。 スレッド安全性:
/// 純粋関数で、どのスレッドからも呼び出し可能。ヒープを確保しない。
void write_rtp_header(const RtpHeader& header, std::span<std::uint8_t> out) noexcept;

}  // namespace vocroz::transport::wire
