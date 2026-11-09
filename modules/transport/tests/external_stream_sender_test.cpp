// SPDX-License-Identifier: AGPL-3.0-only
//
// SRTP 外部出力の送信側のテスト。送ったデータグラムを libsrtp 自身の受信側で復号し、
// 標準の SRTP と RTP の形式で受信機に届くことを確かめる（外部の受信機との相互接続試験の代わり。
// docs/adr/0040-srtp-external-output.md）。

#include "vocroz/transport/external_stream_sender.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <numbers>
#include <span>
#include <vector>

#include <gtest/gtest.h>
#include <srtp.h>

#include "vocroz/transport/opus_frame_decoder.h"
#include "vocroz/transport/opus_frame_encoder.h"

#include "fakes/fake_datagram_socket.h"
#include "fakes/fake_random_source.h"

namespace vocroz::transport {
namespace {

constexpr const char* kKeyText = "inline:WVNfX19zZW1jdGwgKCkgewkyMjA7fQp9CnVubGVz";
constexpr const char* kOtherKeyText = "inline:d0RmdmcmVCspeEc3QGZiNWpVLFJhQX1cfHAwJSoj";
constexpr std::size_t kFrameSamples = frame_samples(FrameDuration::k5000Us);
constexpr std::size_t kTag80Bytes = 10;
constexpr std::size_t kTag32Bytes = 4;

// 受信機の代わりの libsrtp の受信側のセッション。
// srtp_init() は 2 回目の呼び出しで失敗するので、ここでは呼ばず、先に作った ExternalStreamSender の
// 初期化に任せる（各テストは送信側を先に作る）。
class LibsrtpReceiver {
public:
    explicit LibsrtpReceiver(const SrtpMasterKey& key,
                             SrtpCryptoSuite suite = SrtpCryptoSuite::kAesCm128HmacSha1_80) {
        std::array<unsigned char, kSrtpKeyMaterialBytes> material{};
        std::ranges::copy(key.material(), material.begin());
        srtp_policy_t policy;
        std::memset(&policy, 0, sizeof(policy));
        if (suite == SrtpCryptoSuite::kAesCm128HmacSha1_32) {
            srtp_crypto_policy_set_aes_cm_128_hmac_sha1_32(&policy.rtp);
        } else {
            srtp_crypto_policy_set_aes_cm_128_hmac_sha1_80(&policy.rtp);
        }
        srtp_crypto_policy_set_aes_cm_128_hmac_sha1_80(&policy.rtcp);
        policy.ssrc.type = ssrc_any_inbound;
        policy.key = material.data();
        policy.window_size = 128;
        EXPECT_EQ(srtp_create(&session_, &policy), srtp_err_status_ok);
    }
    LibsrtpReceiver(const LibsrtpReceiver&) = delete;
    LibsrtpReceiver& operator=(const LibsrtpReceiver&) = delete;
    ~LibsrtpReceiver() {
        srtp_dealloc(session_);
    }

    // 復号できれば RTP のパケット（ヘッダーとペイロード）を返す。認証に失敗したら空を返す。
    std::vector<std::uint8_t> unprotect(const std::vector<std::uint8_t>& datagram) {
        alignas(4) std::array<std::uint8_t, kMaxPacketBytes> buffer{};
        std::ranges::copy(datagram, buffer.begin());
        int length = static_cast<int>(datagram.size());
        if (srtp_unprotect(session_, buffer.data(), &length) != srtp_err_status_ok) {
            return {};
        }
        return {buffer.begin(), buffer.begin() + length};
    }

private:
    srtp_t session_ = nullptr;
};

struct ParsedRtp {
    std::uint8_t version = 0;
    bool marker = false;
    std::uint8_t payload_type = 0;
    std::uint16_t sequence = 0;
    std::uint32_t timestamp = 0;
    std::uint32_t ssrc = 0;
    std::vector<std::uint8_t> payload;
};

ParsedRtp parse_rtp(const std::vector<std::uint8_t>& packet) {
    ParsedRtp rtp;
    rtp.version = static_cast<std::uint8_t>(packet.at(0) >> 6);
    rtp.marker = (packet.at(1) & 0x80) != 0;
    rtp.payload_type = static_cast<std::uint8_t>(packet.at(1) & 0x7F);
    rtp.sequence = static_cast<std::uint16_t>((packet.at(2) << 8) | packet.at(3));
    rtp.timestamp = (std::uint32_t{packet.at(4)} << 24) | (std::uint32_t{packet.at(5)} << 16) |
                    (std::uint32_t{packet.at(6)} << 8) | std::uint32_t{packet.at(7)};
    rtp.ssrc = (std::uint32_t{packet.at(8)} << 24) | (std::uint32_t{packet.at(9)} << 16) |
               (std::uint32_t{packet.at(10)} << 8) | std::uint32_t{packet.at(11)};
    rtp.payload.assign(packet.begin() + 12, packet.end());
    return rtp;
}

MediaFrame make_frame(std::uint64_t timestamp, std::uint8_t fill, std::size_t size = 40) {
    std::vector<std::uint8_t> payload(size, fill);
    auto frame = make_media_frame(timestamp, 0, payload);
    EXPECT_TRUE(frame);
    return frame.value();
}

RtpStreamParams make_params() {
    RtpStreamParams params;
    params.payload_type = kDefaultOpusPayloadType;
    params.ssrc = 0x0BADCAFE;
    params.initial_sequence = 0xFFFE;  // 折り返しをまたぐ
    params.initial_timestamp = 0xFFFFFF00U;
    params.frame_duration = FrameDuration::k5000Us;
    return params;
}

SrtpMasterKey key_from(const char* text) {
    auto key = parse_srtp_master_key(text);
    EXPECT_TRUE(key);
    return key.value();
}

TEST(ExternalStreamSenderTest, LibsrtpReceiverDecryptsStandardRtpWithConsecutiveSequence) {
    const SrtpMasterKey key = key_from(kKeyText);
    FakeDatagramSocket socket;
    auto sender = ExternalStreamSender::create(SrtpCryptoSuite::kAesCm128HmacSha1_80, key,
                                               make_params(), socket);
    ASSERT_TRUE(sender);
    for (std::uint64_t i = 0; i < 4; ++i) {
        ASSERT_TRUE(
            sender.value().send(make_frame(i * kFrameSamples, static_cast<std::uint8_t>(i + 1))));
    }
    ASSERT_EQ(socket.datagrams.size(), 4U);
    EXPECT_EQ(sender.value().sent_packets(), 4U);

    LibsrtpReceiver receiver(key);
    for (std::size_t i = 0; i < socket.datagrams.size(); ++i) {
        EXPECT_EQ(socket.datagrams[i].size(), 12 + 40 + kTag80Bytes);
        const auto packet = receiver.unprotect(socket.datagrams[i]);
        ASSERT_FALSE(packet.empty()) << i;
        const ParsedRtp rtp = parse_rtp(packet);
        EXPECT_EQ(rtp.version, 2);
        EXPECT_EQ(rtp.marker, i == 0);  // 最初のパケットだけ
        EXPECT_EQ(rtp.payload_type, kDefaultOpusPayloadType);
        EXPECT_EQ(rtp.sequence, static_cast<std::uint16_t>(0xFFFE + i));
        EXPECT_EQ(rtp.timestamp, static_cast<std::uint32_t>(0xFFFFFF00U + i * kFrameSamples));
        EXPECT_EQ(rtp.ssrc, 0x0BADCAFEU);
        EXPECT_EQ(rtp.payload, std::vector<std::uint8_t>(40, static_cast<std::uint8_t>(i + 1)));
    }
}

TEST(ExternalStreamSenderTest, PayloadIsNeverSentInPlaintext) {
    const SrtpMasterKey key = key_from(kKeyText);
    FakeDatagramSocket socket;
    auto sender = ExternalStreamSender::create(SrtpCryptoSuite::kAesCm128HmacSha1_80, key,
                                               make_params(), socket);
    ASSERT_TRUE(sender);
    ASSERT_TRUE(sender.value().send(make_frame(0, 0x55, 64)));
    ASSERT_EQ(socket.datagrams.size(), 1U);
    const auto& datagram = socket.datagrams[0];
    const std::vector<std::uint8_t> plaintext(64, 0x55);
    // 暗号化したペイロードに、元のペイロードの連続した 8 バイトが残っていないこと。
    const std::vector<std::uint8_t> probe(plaintext.begin(), plaintext.begin() + 8);
    EXPECT_TRUE(
        std::ranges::search(datagram.begin() + 12, datagram.end(), probe.begin(), probe.end())
            .empty());
}

TEST(ExternalStreamSenderTest, ReceiverWithAnotherKeyRejectsThePackets) {
    FakeDatagramSocket socket;
    auto sender = ExternalStreamSender::create(SrtpCryptoSuite::kAesCm128HmacSha1_80,
                                               key_from(kKeyText), make_params(), socket);
    ASSERT_TRUE(sender);
    ASSERT_TRUE(sender.value().send(make_frame(0, 0x11)));
    LibsrtpReceiver receiver(key_from(kOtherKeyText));
    EXPECT_TRUE(receiver.unprotect(socket.datagrams.at(0)).empty());
}

TEST(ExternalStreamSenderTest, TamperedPacketFailsAuthentication) {
    const SrtpMasterKey key = key_from(kKeyText);
    FakeDatagramSocket socket;
    auto sender = ExternalStreamSender::create(SrtpCryptoSuite::kAesCm128HmacSha1_80, key,
                                               make_params(), socket);
    ASSERT_TRUE(sender);
    ASSERT_TRUE(sender.value().send(make_frame(0, 0x11)));
    auto datagram = socket.datagrams.at(0);
    datagram[20] ^= 0x01;
    LibsrtpReceiver receiver(key);
    EXPECT_TRUE(receiver.unprotect(datagram).empty());
}

TEST(ExternalStreamSenderTest, ShortTagSuiteAppendsFourByteTagAndDecrypts) {
    const SrtpMasterKey key = key_from(kKeyText);
    FakeDatagramSocket socket;
    auto sender = ExternalStreamSender::create(SrtpCryptoSuite::kAesCm128HmacSha1_32, key,
                                               make_params(), socket);
    ASSERT_TRUE(sender);
    ASSERT_TRUE(sender.value().send(make_frame(0, 0x22)));
    ASSERT_EQ(socket.datagrams.at(0).size(), 12 + 40 + kTag32Bytes);
    LibsrtpReceiver receiver(key, SrtpCryptoSuite::kAesCm128HmacSha1_32);
    EXPECT_FALSE(receiver.unprotect(socket.datagrams.at(0)).empty());
}

TEST(ExternalStreamSenderTest, SetsMarkerAgainAfterAGapInTimestamps) {
    const SrtpMasterKey key = key_from(kKeyText);
    FakeDatagramSocket socket;
    auto sender = ExternalStreamSender::create(SrtpCryptoSuite::kAesCm128HmacSha1_80, key,
                                               make_params(), socket);
    ASSERT_TRUE(sender);
    ASSERT_TRUE(sender.value().send(make_frame(0, 1)));
    ASSERT_TRUE(sender.value().send(make_frame(kFrameSamples, 2)));
    // 10 フレームぶん飛ぶ（ミュートの間はエンコーダーがタイムスタンプだけ進める）。
    ASSERT_TRUE(sender.value().send(make_frame(12 * kFrameSamples, 3)));
    ASSERT_TRUE(sender.value().send(make_frame(13 * kFrameSamples, 4)));
    LibsrtpReceiver receiver(key);
    std::vector<bool> markers;
    std::vector<std::uint16_t> sequences;
    for (const auto& datagram : socket.datagrams) {
        const ParsedRtp rtp = parse_rtp(receiver.unprotect(datagram));
        markers.push_back(rtp.marker);
        sequences.push_back(rtp.sequence);
    }
    EXPECT_EQ(markers, (std::vector<bool>{true, false, true, false}));
    // シーケンス番号は途切れの間も連続する（RFC 3550。欠落と区別するため）。
    EXPECT_EQ(sequences, (std::vector<std::uint16_t>{0xFFFE, 0xFFFF, 0x0000, 0x0001}));
}

TEST(ExternalStreamSenderTest, SocketFailureDropsThePacketAndStillAdvancesSequence) {
    const SrtpMasterKey key = key_from(kKeyText);
    FakeDatagramSocket socket;
    auto sender = ExternalStreamSender::create(SrtpCryptoSuite::kAesCm128HmacSha1_80, key,
                                               make_params(), socket);
    ASSERT_TRUE(sender);
    socket.fail_sends = true;
    const auto failed = sender.value().send(make_frame(0, 1));
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error(), ExternalStreamError::kSendFailed);
    EXPECT_EQ(sender.value().dropped_packets(), 1U);
    socket.fail_sends = false;
    ASSERT_TRUE(sender.value().send(make_frame(kFrameSamples, 2)));
    LibsrtpReceiver receiver(key);
    const ParsedRtp rtp = parse_rtp(receiver.unprotect(socket.datagrams.at(0)));
    EXPECT_EQ(rtp.sequence, 0xFFFF);
}

TEST(ExternalStreamSenderTest, RejectsPayloadTypeOutsideTheDynamicRange) {
    FakeDatagramSocket socket;
    RtpStreamParams params = make_params();
    for (const std::uint8_t payload_type : {std::uint8_t{0}, std::uint8_t{95}, std::uint8_t{128}}) {
        params.payload_type = payload_type;
        const auto sender = ExternalStreamSender::create(SrtpCryptoSuite::kAesCm128HmacSha1_80,
                                                         key_from(kKeyText), params, socket);
        ASSERT_FALSE(sender);
        EXPECT_EQ(sender.error(), ExternalStreamError::kInvalidPayloadType);
    }
    EXPECT_TRUE(socket.datagrams.empty());
}

TEST(ExternalStreamSenderTest, StreamParamsTakeSsrcAndInitialValuesFromRandomSource) {
    FakeRandomSource random;
    random.value = 0x12345678;
    const RtpStreamParams params = make_rtp_stream_params(random, 100, FrameDuration::k2500Us);
    EXPECT_EQ(params.payload_type, 100);
    EXPECT_EQ(params.ssrc, 0x12345678U);
    EXPECT_EQ(params.initial_sequence, 0x5678);
    EXPECT_EQ(params.initial_timestamp, 0x12345678U);
    EXPECT_EQ(params.frame_duration, FrameDuration::k2500Us);
    EXPECT_EQ(random.calls, 3);
}

// エンコード → RTP → SRTP → libsrtp で復号 →
// デコードまでを通し、ステレオの試験音が届くことを確かめる。
TEST(ExternalStreamSenderTest, StereoOpusToneSurvivesTheLoopback) {
    const SrtpMasterKey key = key_from(kKeyText);
    OpusEncoderConfig config;
    config.channels = 2;
    config.bitrate_bps = kStereoBitrateBps;
    auto encoder = OpusFrameEncoder::create(config);
    ASSERT_TRUE(encoder);
    auto decoder = OpusFrameDecoder::create(2);
    ASSERT_TRUE(decoder);
    FakeDatagramSocket socket;
    auto sender = ExternalStreamSender::create(SrtpCryptoSuite::kAesCm128HmacSha1_80, key,
                                               make_params(), socket);
    ASSERT_TRUE(sender);
    LibsrtpReceiver receiver(key);

    // 1 kHz、-20 dBFS（振幅 0.1）を左だけに入れ、右は無音にする。
    constexpr int kFrames = 40;  // 200 ms。エンコーダーとデコーダーの立ち上がりを越える長さ
    constexpr float kAmplitude = 0.1F;
    double left_energy = 0.0;
    double right_energy = 0.0;
    for (int f = 0; f < kFrames; ++f) {
        std::array<float, kFrameSamples * 2> pcm{};
        for (std::size_t n = 0; n < kFrameSamples; ++n) {
            const double t = static_cast<double>(f * kFrameSamples + n) / kSampleRate;
            pcm[n * 2] =
                kAmplitude * static_cast<float>(std::sin(2.0 * std::numbers::pi * 1000.0 * t));
        }
        std::array<std::uint8_t, kMaxOpusPayloadBytes> payload{};
        const auto size = encoder.value().encode(pcm, payload);
        ASSERT_TRUE(size);
        auto frame = make_media_frame(static_cast<std::uint64_t>(f) * kFrameSamples, 0,
                                      std::span<const std::uint8_t>(payload).first(size.value()));
        ASSERT_TRUE(frame);
        ASSERT_TRUE(sender.value().send(frame.value()));

        const ParsedRtp rtp = parse_rtp(receiver.unprotect(socket.datagrams.back()));
        std::array<float, kFrameSamples * 2> decoded{};
        ASSERT_TRUE(decoder.value().decode(rtp.payload, decoded));
        if (f >= kFrames / 2) {
            for (std::size_t n = 0; n < kFrameSamples; ++n) {
                left_energy += decoded[n * 2] * decoded[n * 2];
                right_energy += decoded[n * 2 + 1] * decoded[n * 2 + 1];
            }
        }
    }
    const double samples = static_cast<double>(kFrames / 2 * kFrameSamples);
    const double left_rms = std::sqrt(left_energy / samples);
    // 正弦波の RMS は振幅 / √2。符号化の誤差を見込んで 2 割の幅で比べる。
    EXPECT_NEAR(left_rms, kAmplitude / std::numbers::sqrt2, kAmplitude / std::numbers::sqrt2 * 0.2);
    EXPECT_LT(std::sqrt(right_energy / samples), left_rms * 0.1);
}

}  // namespace
}  // namespace vocroz::transport
