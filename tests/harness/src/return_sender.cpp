// SPDX-License-Identifier: AGPL-3.0-only
//
// ReturnSender の実装。

#include "vocroz/test_harness/return_sender.h"

#include <utility>

#include "vocroz/transport/constants.h"

namespace vocroz::test_harness {

common::Result<std::unique_ptr<ReturnSender>, transport::CodecError> ReturnSender::create(
    transport::PacketSealer sealer, transport::DatagramSocket& socket,
    const transport::OpusEncoderConfig& config, std::uint32_t mute_epoch) {
    auto encoder = transport::OpusFrameEncoder::create(config);
    if (!encoder) {
        return common::make_unexpected(encoder.error());
    }
    auto sender =
        std::make_unique<ReturnSender>(std::move(encoder).value(), std::move(sealer), socket);
    // 返送は初期ミュートなので、親機のオペレーターが解除した状態にする。
    static_cast<void>(sender->encoder_.set_params(
        transport::StreamSendParams{.muted = false, .mute_epoch = mute_epoch, .gain = 1.0F}));
    return sender;
}

ReturnSender::ReturnSender(transport::OpusFrameEncoder encoder, transport::PacketSealer sealer,
                           transport::DatagramSocket& socket)
    : lookahead_samples_(encoder.lookahead_samples()),
      packets_(std::move(sealer), socket, transport::kReturnStreamId, notifier_),
      encoder_(std::move(encoder), packets_) {}

void ReturnSender::push(std::span<const float> block) {
    encoder_.push(block);
    packets_.pump();
}

}  // namespace vocroz::test_harness
