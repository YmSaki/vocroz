// SPDX-License-Identifier: AGPL-3.0-only
//
// ExternalStreamSender の実装。RTP のパケットを 1 つのバッファの中で組み立てて、その場で SRTP に
// 暗号化してから送る。暗号化の前のバイト列をソケットに渡す変数を持たないことで、平文の誤送信を防ぐ。

#include "vocroz/transport/external_stream_sender.h"

#include <algorithm>
#include <array>
#include <utility>

#include "srtp_protector.h"
#include "wire/rtp_header_codec.h"

namespace vocroz::transport {

RtpStreamParams make_rtp_stream_params(RandomSource& random, std::uint8_t payload_type,
                                       FrameDuration frame_duration) noexcept {
    RtpStreamParams params;
    params.payload_type = payload_type;
    params.ssrc = random.next_uint32();
    params.initial_sequence = static_cast<std::uint16_t>(random.next_uint32());
    params.initial_timestamp = random.next_uint32();
    params.frame_duration = frame_duration;
    return params;
}

common::Result<ExternalStreamSender, ExternalStreamError> ExternalStreamSender::create(
    SrtpCryptoSuite suite, const SrtpMasterKey& key, const RtpStreamParams& params,
    DatagramSocket& socket) noexcept {
    if (params.payload_type < kMinDynamicPayloadType ||
        params.payload_type > kMaxDynamicPayloadType) {
        return common::make_unexpected(ExternalStreamError::kInvalidPayloadType);
    }
    auto protector = SrtpProtector::create(suite, key, params.ssrc);
    if (!protector) {
        return common::make_unexpected(protector.error());
    }
    return ExternalStreamSender(std::make_unique<SrtpProtector>(std::move(protector.value())),
                                params, socket);
}

ExternalStreamSender::ExternalStreamSender(std::unique_ptr<SrtpProtector> protector,
                                           const RtpStreamParams& params,
                                           DatagramSocket& socket) noexcept
    : protector_(std::move(protector)),
      params_(params),
      socket_(&socket),
      next_sequence_(params.initial_sequence) {}

ExternalStreamSender::ExternalStreamSender(ExternalStreamSender&&) noexcept = default;
ExternalStreamSender& ExternalStreamSender::operator=(ExternalStreamSender&&) noexcept = default;
ExternalStreamSender::~ExternalStreamSender() = default;

common::Result<void, ExternalStreamError> ExternalStreamSender::send(
    const MediaFrame& frame) noexcept {
    const auto payload = frame.payload_bytes();
    const std::size_t rtp_length = wire::kRtpHeaderBytes + payload.size();

    // 1 フレームより先へ飛んだら、送信が途切れていた（ミュートや送信待ちの溢れ）とみなす。
    const bool resumed =
        !last_timestamp_ ||
        frame.timestamp != *last_timestamp_ + frame_samples(params_.frame_duration);
    last_timestamp_ = frame.timestamp;

    // シーケンス番号は、送れたかどうかに関わらず進める。libsrtp
    // は同じ番号の再暗号化を拒否するので、 進めないと以降のパケットがすべて失敗する。
    const std::uint16_t sequence = next_sequence_++;

    if (rtp_length + kSrtpMaxTrailerBytes > kMaxPacketBytes) {
        ++dropped_packets_;
        return common::make_unexpected(ExternalStreamError::kPayloadTooLarge);
    }

    // libsrtp がヘッダーを 32 ビット単位で読むので、先頭を 4 バイト境界にそろえる。
    alignas(4) std::array<std::uint8_t, kMaxPacketBytes> packet{};
    wire::RtpHeader header;
    header.marker = resumed;
    header.payload_type = params_.payload_type;
    header.sequence = sequence;
    // RTP のタイムスタンプは 32 ビットで折り返す（RFC 3550）。
    header.timestamp =
        params_.initial_timestamp + static_cast<std::uint32_t>(frame.timestamp & 0xFFFFFFFFU);
    header.ssrc = params_.ssrc;
    wire::write_rtp_header(header, packet);
    std::copy(payload.begin(), payload.end(), packet.begin() + wire::kRtpHeaderBytes);

    const auto protected_length = protector_->protect_rtp(packet, rtp_length);
    if (!protected_length) {
        ++dropped_packets_;
        return common::make_unexpected(protected_length.error());
    }
    const auto sent =
        socket_->send(std::span<const std::uint8_t>(packet).first(protected_length.value()));
    if (!sent) {
        ++dropped_packets_;
        return common::make_unexpected(ExternalStreamError::kSendFailed);
    }
    ++sent_packets_;
    return {};
}

}  // namespace vocroz::transport
