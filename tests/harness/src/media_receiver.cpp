// SPDX-License-Identifier: AGPL-3.0-only
//
// MediaReceiver の実装。

#include "vocroz/test_harness/media_receiver.h"

#include <utility>

#include "vocroz/transport/constants.h"
#include "vocroz/transport/media_frame.h"
#include "vocroz/transport/media_payload.h"

namespace vocroz::test_harness {

common::Result<std::unique_ptr<MediaReceiver>, transport::PlayoutError> MediaReceiver::create(
    transport::PacketOpener& opener, const transport::PlayoutConfig& config,
    common::Clock::TimePoint now) {
    auto playout = transport::PlayoutStream::create(config);
    if (!playout) {
        return common::make_unexpected(playout.error());
    }
    return std::make_unique<MediaReceiver>(opener, std::move(playout).value(), now);
}

MediaReceiver::MediaReceiver(transport::PacketOpener& opener,
                             std::unique_ptr<transport::PlayoutStream> playout,
                             common::Clock::TimePoint now)
    : opener_(opener),
      playout_(std::move(playout)),
      monitor_(now),
      plaintext_(transport::kMaxPacketBytes) {}

void MediaReceiver::unmute(std::uint32_t mute_epoch, common::Clock::TimePoint now) {
    on_send_mute(false, mute_epoch, now);
}

void MediaReceiver::on_datagram(std::span<const std::uint8_t> datagram,
                                common::Clock::TimePoint now) {
    ++stats_.datagrams;
    const auto opened = opener_.open(datagram, plaintext_);
    if (!opened) {
        if (opened.error() == transport::TransportError::kReplayed) {
            ++stats_.rejected_replay;
        } else {
            ++stats_.rejected_invalid;
        }
        return;
    }
    if (opened.value().type != transport::PacketType::kMedia) {
        // 制御パケットは信頼制御層が扱う。このパイプラインには流さない。
        return;
    }
    const auto media = transport::decode_media_payload(opened.value().plaintext);
    if (!media) {
        ++stats_.rejected_invalid;
        return;
    }
    on_media(media.value(), now);
}

void MediaReceiver::on_send_mute(bool send_muted, std::uint32_t mute_epoch,
                                 common::Clock::TimePoint now) {
    monitor_.on_send_mute(send_muted, mute_epoch, now);
}

void MediaReceiver::on_media(const transport::MediaPayload& media, common::Clock::TimePoint now) {
    if (!monitor_.on_media_frame(media.mute_epoch, now)) {
        ++stats_.rejected_by_signal;
        return;
    }
    const auto frame =
        transport::make_media_frame(media.timestamp, media.mute_epoch, media.opus_payload);
    if (!frame) {
        ++stats_.rejected_invalid;
        return;
    }
    ++stats_.media_frames;
    playout_->enqueue(frame.value());
}

void MediaReceiver::poll(common::Clock::TimePoint now) {
    monitor_.poll(now);
}

void MediaReceiver::render(std::span<float> out) {
    playout_->render(out);
}

void MediaReceiver::render(std::span<float> left, std::span<float> right) {
    playout_->render(left, right);
}

}  // namespace vocroz::test_harness
