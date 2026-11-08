// SPDX-License-Identifier: AGPL-3.0-only
//
// メディアパケットの平文のパースとシリアライズ。
// 認証済みの平文でも、送信側の実装の誤りや将来のバージョンの違いに備えて形式を検証する。

#include "wire/media_payload_codec.h"

#include <algorithm>

#include "detail/byte_order_utils.h"

namespace vocroz::transport {
namespace {

// 平文内の各フィールドの位置（バイト）。
constexpr std::size_t kStreamIdOffset = 0;
constexpr std::size_t kTimestampOffset = 2;
constexpr std::size_t kMuteEpochOffset = 10;

}  // namespace

common::Result<MediaPayload, TransportError> decode_media_payload(
    std::span<const std::uint8_t> plaintext) noexcept {
    // DTX を使わないので、Opus のペイロードが空のフレームは正規の送信側からは届かない。
    if (plaintext.size() <= kMediaPayloadHeaderBytes) {
        return common::make_unexpected(TransportError::kMalformedPayload);
    }
    return MediaPayload{
        .stream_id = detail::load_big_endian<std::uint16_t>(plaintext.subspan(kStreamIdOffset)),
        .timestamp = detail::load_big_endian<std::uint64_t>(plaintext.subspan(kTimestampOffset)),
        .mute_epoch = detail::load_big_endian<std::uint32_t>(plaintext.subspan(kMuteEpochOffset)),
        .opus_payload = plaintext.subspan(kMediaPayloadHeaderBytes),
    };
}

namespace wire {

std::size_t write_media_payload(const MediaPayload& payload, std::span<std::uint8_t> out) noexcept {
    detail::store_big_endian(payload.stream_id, out.subspan(kStreamIdOffset));
    detail::store_big_endian(payload.timestamp, out.subspan(kTimestampOffset));
    detail::store_big_endian(payload.mute_epoch, out.subspan(kMuteEpochOffset));
    std::copy(payload.opus_payload.begin(), payload.opus_payload.end(),
              out.begin() + static_cast<std::ptrdiff_t>(kMediaPayloadHeaderBytes));
    return media_payload_bytes(payload);
}

}  // namespace wire
}  // namespace vocroz::transport
