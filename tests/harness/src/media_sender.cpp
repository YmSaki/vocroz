// SPDX-License-Identifier: AGPL-3.0-only
//
// MediaSender の実装。

#include "vocroz/test_harness/media_sender.h"

#include <utility>

#include "vocroz/transport/constants.h"
#include "vocroz/transport/media_payload.h"

namespace vocroz::test_harness {

common::Result<MediaSender, transport::CodecError> MediaSender::create(
    transport::PacketSealer& sealer, transport::DatagramSocket& socket,
    const transport::OpusEncoderConfig& config, std::uint16_t stream_id, std::uint32_t mute_epoch) {
    auto encoder = transport::OpusFrameEncoder::create(config);
    if (!encoder) {
        return common::make_unexpected(encoder.error());
    }
    return MediaSender(sealer, socket, std::move(encoder).value(), stream_id, mute_epoch);
}

MediaSender::MediaSender(transport::PacketSealer& sealer, transport::DatagramSocket& socket,
                         transport::OpusFrameEncoder encoder, std::uint16_t stream_id,
                         std::uint32_t mute_epoch)
    : sealer_(&sealer),
      socket_(&socket),
      encoder_(std::move(encoder)),
      stream_id_(stream_id),
      mute_epoch_(mute_epoch),
      payload_(transport::kMaxOpusPayloadBytes),
      packet_(transport::kMaxPacketBytes) {
    pending_.reserve(encoder_.frame_samples() * 2);
}

void MediaSender::capture(std::span<const float> block) {
    pending_.insert(pending_.end(), block.begin(), block.end());
    while (pending_.size() >= encoder_.frame_samples()) {
        send_frame();
        pending_.erase(pending_.begin(),
                       pending_.begin() + static_cast<std::ptrdiff_t>(encoder_.frame_samples()));
    }
}

void MediaSender::send_frame() {
    const std::span<const float> pcm = std::span(pending_).first(encoder_.frame_samples());
    const std::uint64_t timestamp = next_timestamp_;
    // エンコードや送信に失敗してもタイムスタンプは進める。送信側の時刻は実時間に従うので、
    // 失敗したフレームは受信側から見て欠損と同じに扱われる。
    next_timestamp_ += encoder_.frame_samples();
    const auto encoded = encoder_.encode(pcm, payload_);
    if (!encoded) {
        ++stats_.encode_failures;
        return;
    }
    const transport::MediaPayload media{
        .stream_id = stream_id_,
        .timestamp = timestamp,
        .mute_epoch = mute_epoch_,
        .opus_payload = std::span<const std::uint8_t>(payload_).first(encoded.value()),
    };
    const auto sealed = sealer_->seal_media(media, packet_);
    if (!sealed || !socket_->send(std::span<const std::uint8_t>(packet_).first(sealed.value()))) {
        ++stats_.send_failures;
        return;
    }
    ++stats_.frames_sent;
}

}  // namespace vocroz::test_harness
