// SPDX-License-Identifier: AGPL-3.0-only
//
// 制御パケットの平文の読み書き。バイト配置は docs/adr/0019-reliable-control-layer.md のとおりで、
// 整数はほかのフレームと同じくビッグエンディアンにする。

#include "wire/control_frame_codec.h"

#include <algorithm>

#include "detail/byte_order_utils.h"
#include "transport_constants.h"

namespace vocroz::transport::wire {
namespace {

// レーンの値がワイヤー上で定義済みかを確かめる。未知のレーンは将来の拡張か改変の印なので受け付けない。
[[nodiscard]] bool is_known_lane(std::uint8_t value) noexcept {
    return value < kControlLaneCount;
}

}  // namespace

common::Result<ControlFrames, TransportError> parse_control_frames(
    std::span<const std::uint8_t> plaintext) {
    ControlFrames frames;
    std::size_t offset = 0;
    while (offset < plaintext.size()) {
        const auto rest = plaintext.subspan(offset);
        // 種別とレーンの 2 バイトはどちらのフレームにもある。
        if (rest.size() < 2 || !is_known_lane(rest[1])) {
            return common::make_unexpected(TransportError::kMalformedPayload);
        }
        const auto lane = static_cast<ControlLane>(rest[1]);
        if (rest[0] == kAckFrameType) {
            if (rest.size() < kAckFrameBytes) {
                return common::make_unexpected(TransportError::kMalformedPayload);
            }
            frames.acks.push_back(AckFrame{
                .lane = lane,
                .cumulative = detail::load_big_endian<std::uint32_t>(rest.subspan(2)),
                .selective = detail::load_big_endian<std::uint64_t>(rest.subspan(6)),
            });
            offset += kAckFrameBytes;
        } else if (rest[0] == kDataFrameType) {
            if (rest.size() < kDataFrameHeaderBytes) {
                return common::make_unexpected(TransportError::kMalformedPayload);
            }
            const auto length = detail::load_big_endian<std::uint16_t>(rest.subspan(6));
            if (rest.size() - kDataFrameHeaderBytes < length) {
                return common::make_unexpected(TransportError::kMalformedPayload);
            }
            frames.data.push_back(DataFrame{
                .lane = lane,
                .sequence = detail::load_big_endian<std::uint32_t>(rest.subspan(2)),
                .message = rest.subspan(kDataFrameHeaderBytes, length),
            });
            offset += kDataFrameHeaderBytes + length;
        } else {
            return common::make_unexpected(TransportError::kMalformedPayload);
        }
    }
    return frames;
}

bool ControlFrameWriter::append_ack(const AckFrame& frame) noexcept {
    if (buffer_.size() - size_ < kAckFrameBytes) {
        return false;
    }
    const auto out = std::span(buffer_).subspan(size_, kAckFrameBytes);
    out[0] = kAckFrameType;
    out[1] = static_cast<std::uint8_t>(frame.lane);
    detail::store_big_endian(frame.cumulative, out.subspan(2));
    detail::store_big_endian(frame.selective, out.subspan(6));
    size_ += kAckFrameBytes;
    return true;
}

bool ControlFrameWriter::append_data(const DataFrame& frame) noexcept {
    const auto frame_bytes = kDataFrameHeaderBytes + frame.message.size();
    if (frame.message.size() > kMaxControlMessageBytes || buffer_.size() - size_ < frame_bytes) {
        return false;
    }
    const auto out = std::span(buffer_).subspan(size_, frame_bytes);
    out[0] = kDataFrameType;
    out[1] = static_cast<std::uint8_t>(frame.lane);
    detail::store_big_endian(frame.sequence, out.subspan(2));
    detail::store_big_endian(static_cast<std::uint16_t>(frame.message.size()), out.subspan(6));
    std::ranges::copy(frame.message, out.subspan(kDataFrameHeaderBytes).begin());
    size_ += frame_bytes;
    return true;
}

}  // namespace vocroz::transport::wire
