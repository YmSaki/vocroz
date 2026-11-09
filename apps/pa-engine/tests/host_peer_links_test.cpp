// SPDX-License-Identifier: AGPL-3.0-only
//
// HostPeerLinks と PeerReceivePath の単体テスト。受信スレッドとコントロールスレッドの間の
// 中継（PeerReceiveRelay）の代わりに同じスレッドでそのまま渡し、本物の鍵交換、信頼制御層、
// Opus、ジッターバッファ、ミキサーを通して、子機の音がストリップから出るまでを確かめる。

#include "host_peer_links.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "vocroz/audio_core/constants.h"
#include "vocroz/audio_core/mixer.h"
#include "vocroz/network/host_rendezvous.h"
#include "vocroz/session/peer_midi_inbox.h"
#include "vocroz/transport/constants.h"
#include "vocroz/transport/control_lane.h"
#include "vocroz/transport/media_payload.h"
#include "vocroz/transport/noise_ik_handshake.h"
#include "vocroz/transport/opus_frame_encoder.h"
#include "vocroz/transport/ports/control_message_listener.h"
#include "vocroz/transport/reliable_control_channel.h"
#include "vocroz/transport/static_key_pair.h"

#include "engine_constants.h"
#include "engine_mixer.h"
#include "fakes/fake_clock.h"
#include "fakes/fake_control_packet_sink.h"
#include "fakes/fake_logger.h"
#include "fakes/fake_random_source.h"
#include "fakes/fake_udp_endpoint.h"
#include "peer_event_sink.h"
#include "peer_receive_path.h"

namespace vocroz::engine {
namespace {

namespace ac = audio_core;

// 時計のない 1 ブロック（イベントはすべてブロックの先頭に置かれる）。
ac::MidiBlockClock block_clock() {
    ac::MidiBlockClock clock;
    clock.frames = ac::kBlockSize;
    return clock;
}
namespace cp = control_protocol;
namespace net = network;
namespace tp = transport;
using namespace std::chrono_literals;

const net::UdpAddress kClientAddress = net::UdpAddress::from_ipv4({203, 0, 113, 10}, 50000);
const net::UdpAddress kOtherPath = net::UdpAddress::from_ipv4({192, 168, 1, 10}, 50000);
// MIDI レーンのメッセージ: 種別（MIDI イベント）、子機の時刻（µs、u32 BE）、Note On。
const std::vector<std::uint8_t> kNoteOn{0x40, 0, 0, 0, 0, 0x90, 60, 100};
const net::UdpAddress kRebound = net::UdpAddress::from_ipv4({203, 0, 113, 10}, 50123);
constexpr std::uint16_t kMicStreamId = 0;

tp::StaticKeyPair generate_keys() {
    auto keys = tp::StaticKeyPair::generate();
    EXPECT_TRUE(keys);
    return std::move(keys).value();
}

// 受信スレッドの通知を、同じスレッドでそのまま受け取り手へ渡す。
class DirectRelay final : public PeerReceiveListener {
public:
    void on_handshake_initiation(const net::UdpAddress& from,
                                 std::span<const std::uint8_t> datagram) override {
        target->on_handshake_initiation(from, datagram);
    }
    void on_control_packet(session::PeerHandle peer,
                           std::span<const std::uint8_t> plaintext) override {
        target->on_control_packet(peer, plaintext);
    }
    void on_signal_state(session::PeerHandle peer, tp::SignalState state) override {
        target->on_signal_state(peer, state);
    }
    void on_receive_released(session::PeerHandle peer) override {
        target->on_receive_released(peer);
    }

    PeerReceiveListener* target = nullptr;
};

// session への通知を記録する。
class RecordingEvents final : public PeerEventSink {
public:
    void on_peer_message(session::PeerHandle peer, std::span<const std::uint8_t> message) override {
        messages.push_back({peer, {message.begin(), message.end()}});
    }
    void on_peer_established(session::PeerHandle peer) override {
        log.push_back("established:" + std::to_string(peer));
    }
    void on_peer_path_failed(session::PeerHandle peer) override {
        log.push_back("path_failed:" + std::to_string(peer));
    }
    void on_peer_timeout(session::PeerHandle peer) override {
        log.push_back("timeout:" + std::to_string(peer));
    }
    void on_peer_resumed(session::PeerHandle peer) override {
        log.push_back("resumed:" + std::to_string(peer));
    }
    void on_peer_closed(session::PeerHandle peer) override {
        log.push_back("closed:" + std::to_string(peer));
    }
    void on_signal_state(session::PeerHandle peer, cp::SignalState state) override {
        signals.push_back({peer, state});
    }

    struct Message {
        session::PeerHandle peer;
        std::vector<std::uint8_t> bytes;
    };
    struct Signal {
        session::PeerHandle peer;
        cp::SignalState state;
    };
    std::vector<std::string> log;
    std::vector<Message> messages;
    std::vector<Signal> signals;
};

class NullRendezvousListener final : public net::HostRendezvousListener {
public:
    void on_rendezvous_available() override {}
    void on_rendezvous_unavailable(net::NetworkError /*cause*/) override {}
};

// 子機の側。鍵交換の開始側、信頼制御層、メディアの暗号化と Opus のエンコードを持つ。
class TestClient final : public tp::ControlMessageListener {
public:
    TestClient(const common::Clock& clock, const tp::PublicKey& host_key)
        : keys_(generate_keys()), control_(clock, sink_, *this) {
        auto handshake = tp::NoiseIkHandshake::create_initiator(
            keys_, host_key, tp::generate_connection_id().value());
        EXPECT_TRUE(handshake);
        handshake_.emplace(std::move(handshake).value());
        initiation.resize(tp::kMaxPacketBytes);
        initiation.resize(handshake_->write_initiation({}, initiation).value());
        for (std::size_t channels = 1; channels <= 2; ++channels) {
            auto encoder =
                tp::OpusFrameEncoder::create(tp::OpusEncoderConfig{.channels = channels});
            EXPECT_TRUE(encoder);
            encoders_[channels - 1].emplace(std::move(encoder).value());
        }
    }

    // 親機の応答を読み、暗号化セッションを確立する。
    [[nodiscard]] bool finish(std::span<const std::uint8_t> response) {
        std::vector<std::uint8_t> payload(response.size());
        if (!handshake_->read_response(response, payload)) {
            return false;
        }
        auto secure = handshake_->finish();
        if (!secure) {
            return false;
        }
        channel_.emplace(std::move(secure).value());
        return true;
    }

    // 制御メッセージを信頼制御層に載せ、暗号化したパケットを返す。
    [[nodiscard]] std::vector<std::uint8_t> control_packet(std::span<const std::uint8_t> message) {
        EXPECT_TRUE(control_.send(tp::ControlLane::kControl, message));
        auto plaintexts = sink_.take();
        EXPECT_EQ(plaintexts.size(), 1U);
        return seal_control(plaintexts.back());
    }

    // MIDI のメッセージを信頼制御層の MIDI レーンに載せ、暗号化したパケットを返す。
    [[nodiscard]] std::vector<std::uint8_t> midi_packet(std::span<const std::uint8_t> message) {
        EXPECT_TRUE(control_.send(tp::ControlLane::kMidi, message));
        auto plaintexts = sink_.take();
        EXPECT_EQ(plaintexts.size(), 1U);
        return seal_control(plaintexts.back());
    }

    // 440 Hz の正弦波を 1 フレームぶんエンコードし、暗号化したメディアのパケットを返す。
    [[nodiscard]] std::vector<std::uint8_t> media_packet(std::uint32_t mute_epoch,
                                                         std::uint16_t stream_id = kMicStreamId,
                                                         std::size_t channels = 1) {
        tp::OpusFrameEncoder& encoder = *encoders_[channels - 1];
        std::vector<float> pcm(encoder.frame_samples() * channels);
        for (std::size_t i = 0; i < encoder.frame_samples(); ++i) {
            const auto sample =
                0.5F * static_cast<float>(std::sin(2.0 * std::numbers::pi * 440.0 *
                                                   static_cast<double>(phase_ + i) /
                                                   static_cast<double>(tp::kSampleRate)));
            for (std::size_t c = 0; c < channels; ++c) {
                pcm[i * channels + c] = sample;
            }
        }
        phase_ += encoder.frame_samples();
        std::array<std::uint8_t, tp::kMaxOpusPayloadBytes> opus{};
        const auto encoded = encoder.encode(pcm, opus);
        EXPECT_TRUE(encoded);
        const tp::MediaPayload media{.stream_id = stream_id,
                                     .timestamp = timestamp_,
                                     .mute_epoch = mute_epoch,
                                     .opus_payload = std::span(opus).first(encoded.value())};
        timestamp_ += encoder.frame_samples();
        std::vector<std::uint8_t> packet(tp::kMaxPacketBytes);
        packet.resize(channel_->sealer.seal_media(media, packet).value());
        return packet;
    }

    // 親機から届いたパケットを復号し、制御メッセージを取り出す。
    void receive(std::span<const std::uint8_t> packet) {
        std::vector<std::uint8_t> plaintext(tp::kMaxPacketBytes);
        const auto opened = channel_->opener.open(packet, plaintext);
        if (opened && opened.value().type == tp::PacketType::kControl) {
            static_cast<void>(control_.on_control_packet(opened.value().plaintext));
        }
    }

    void on_control_message(tp::ControlLane /*lane*/,
                            std::span<const std::uint8_t> message) override {
        received.emplace_back(message.begin(), message.end());
    }

    std::vector<std::uint8_t> initiation;
    std::vector<std::vector<std::uint8_t>> received;

private:
    std::vector<std::uint8_t> seal_control(std::span<const std::uint8_t> plaintext) {
        std::vector<std::uint8_t> packet(tp::kMaxPacketBytes);
        packet.resize(channel_->sealer.seal_control(plaintext, packet).value());
        return packet;
    }

    tp::StaticKeyPair keys_;
    tp::FakeControlPacketSink sink_;
    tp::ReliableControlChannel control_;
    std::optional<tp::NoiseIkHandshake> handshake_;
    std::optional<tp::SecureChannel> channel_;
    std::array<std::optional<tp::OpusFrameEncoder>, 2> encoders_;
    std::uint64_t timestamp_ = 0;
    std::size_t phase_ = 0;
};

class HostPeerLinksTest : public ::testing::Test {
protected:
    HostPeerLinksTest()
        : host_keys_(generate_keys()),
          path_(relay_, clock_),
          mixer_(ac::MixerConfig{kMixerFaderCapacity, kMixerMeterCapacity}),
          engine_mixer_(mixer_, logger_),
          midi_inbox_(clock_),
          rendezvous_(endpoint_, random_, rendezvous_listener_),
          links_(HostPeerLinks::Ports{endpoint_, path_, engine_mixer_, midi_inbox_, rendezvous_,
                                      events_, host_keys_, clock_, logger_},
                 tp::PlayoutConfig{}) {
        relay_.target = &links_;
    }

    std::unique_ptr<TestClient> make_client() {
        return std::make_unique<TestClient>(clock_, host_keys_.public_key());
    }

    // 受信スレッドにデータグラムが届いたことにする。
    void deliver(const net::UdpAddress& from, std::span<const std::uint8_t> datagram) {
        path_.on_datagram(from, datagram);
    }

    // 受信スレッドを起こすデータグラムだけを届ける（指示の反映と信号状態の判定）。
    void wake() {
        const std::array<std::uint8_t, 1> wake{0};
        deliver(net::UdpAddress::from_ipv4({127, 0, 0, 1}, endpoint_.port), wake);
    }

    // 子機の鍵交換を済ませ、接続の識別子を返す。
    session::PeerHandle connect(TestClient& client, const net::UdpAddress& from = kClientAddress) {
        endpoint_.sent.clear();
        deliver(from, client.initiation);
        EXPECT_EQ(endpoint_.sent.size(), 1U);
        EXPECT_TRUE(client.finish(endpoint_.sent.back().datagram));
        endpoint_.sent.clear();
        return next_handle_++;
    }

    // マイク 1 本（ストリーム 0、モノラル）を strip へ流す対応。
    static std::vector<session::StreamRoute> mic_to(const std::string& strip) {
        return {session::StreamRoute{.stream_id = kMicStreamId, .channels = 1, .strip_id = strip}};
    }

    // ストリップを作って承認し、ストリップと Master のミュートを解く。
    void admit_audible(session::PeerHandle peer, const std::string& strip) {
        engine_mixer_.add_channel(strip);
        links_.admit(peer, mic_to(strip));
        links_.poll();
        engine_mixer_.set_channel_muted(strip, false);
        engine_mixer_.set_master_muted(false);
    }

    // 子機が 5 ms ごとにフレームを送り、親機が同じ周期で描画するのを 200 ms 続け、
    // L の最大の絶対値を返す。
    float stream_for_a_while(TestClient& client, std::uint16_t stream_id, std::size_t channels) {
        float peak = 0.0F;
        for (int i = 0; i < 40; ++i) {
            deliver(kClientAddress, client.media_packet(1, stream_id, channels));
            peak = std::max(peak, render_peak(tp::kSampleRate / 200));
        }
        return peak;
    }

    // ストリップの MIDI のキューから 1 ブロックぶん取り出す（グラフの代わり）。
    ac::MidiPullResult pull_midi(const std::string& strip, std::vector<ac::MidiEvent>& events) {
        auto queue = midi_inbox_.queue_for(strip);
        EXPECT_NE(queue, nullptr);
        events.assign(16, ac::MidiEvent{});
        const auto result = queue->pull(block_clock(), events);
        events.resize(result.count);
        return result;
    }

    // 20 回ぶん（100 ms）描画し、L の最大の絶対値を返す。
    float render_peak_for_a_while() {
        float peak = 0.0F;
        for (int i = 0; i < 20; ++i) {
            peak = std::max(peak, render_peak(tp::kSampleRate / 200));
        }
        return peak;
    }

    // ミキサーを frames サンプルぶん描画し、L の最大の絶対値を返す。
    float render_peak(std::size_t frames) {
        std::vector<float> out(frames * 2, 0.0F);
        mixer_.render(ac::InterleavedBuffer{out, 2});
        engine_mixer_.collect();
        float peak = 0.0F;
        for (std::size_t i = 0; i < out.size(); i += 2) {
            peak = std::max(peak, std::abs(out[i]));
        }
        return peak;
    }

    common::FakeClock clock_;
    common::FakeLogger logger_;
    net::FakeUdpEndpoint endpoint_;
    tp::FakeRandomSource random_;
    NullRendezvousListener rendezvous_listener_;
    RecordingEvents events_;
    tp::StaticKeyPair host_keys_;
    DirectRelay relay_;
    PeerReceivePath path_;
    ac::Mixer mixer_;
    EngineMixer engine_mixer_;
    session::PeerMidiInbox midi_inbox_;
    net::HostRendezvous rendezvous_;
    HostPeerLinks links_;
    session::PeerHandle next_handle_ = 1;
};

TEST_F(HostPeerLinksTest, AnswersHandshakeAndCreatesConnection) {
    auto client = make_client();
    deliver(kClientAddress, client->initiation);
    ASSERT_EQ(endpoint_.sent.size(), 1U);
    EXPECT_EQ(endpoint_.sent[0].to, kClientAddress);
    EXPECT_TRUE(client->finish(endpoint_.sent[0].datagram));
    EXPECT_EQ(links_.peer_count(), 1U);
    // 承認前は session に何も知らせない（申請は鍵交換の後の制御メッセージで届く）。
    EXPECT_TRUE(events_.log.empty());
}

TEST_F(HostPeerLinksTest, RepeatedInitiationGetsSameResponseWithoutNewConnection) {
    auto client = make_client();
    deliver(kClientAddress, client->initiation);
    deliver(kOtherPath, client->initiation);
    ASSERT_EQ(endpoint_.sent.size(), 2U);
    EXPECT_EQ(endpoint_.sent[1].to, kOtherPath);
    EXPECT_EQ(endpoint_.sent[0].datagram, endpoint_.sent[1].datagram);
    EXPECT_EQ(links_.peer_count(), 1U);
}

TEST_F(HostPeerLinksTest, IgnoresInitiationForAnotherHostKey) {
    const auto other_host = generate_keys();
    TestClient client(clock_, other_host.public_key());
    deliver(kClientAddress, client.initiation);
    EXPECT_TRUE(endpoint_.sent.empty());
    EXPECT_EQ(links_.peer_count(), 0U);
}

TEST_F(HostPeerLinksTest, PassesControlMessagesBothWays) {
    auto client = make_client();
    const auto peer = connect(*client);
    const std::vector<std::uint8_t> request{1, 2, 3};
    deliver(kClientAddress, client->control_packet(request));
    ASSERT_EQ(events_.messages.size(), 1U);
    EXPECT_EQ(events_.messages[0].peer, peer);
    EXPECT_EQ(events_.messages[0].bytes, request);

    const std::vector<std::uint8_t> reply{9, 8};
    links_.send(peer, reply);
    ASSERT_FALSE(endpoint_.sent.empty());
    for (const auto& sent : endpoint_.sent) {
        EXPECT_EQ(sent.to, kClientAddress);
        client->receive(sent.datagram);
    }
    ASSERT_EQ(client->received.size(), 1U);
    EXPECT_EQ(client->received[0], reply);
}

TEST_F(HostPeerLinksTest, AdmittedClientIsHeardOnItsStrip) {
    auto client = make_client();
    const auto peer = connect(*client);
    admit_audible(peer, "ch-1");
    EXPECT_EQ(events_.log, (std::vector<std::string>{"established:1"}));
    // 子機が送信ミュートを解いた（世代 1）。
    links_.apply_send_mute(peer, false, 1);
    for (int i = 0; i < 20; ++i) {
        deliver(kClientAddress, client->media_packet(1));
    }
    float peak = 0.0F;
    for (int i = 0; i < 20; ++i) {
        peak = std::max(peak, render_peak(tp::kSampleRate / 200));
    }
    EXPECT_GT(peak, 0.05F);
    ASSERT_FALSE(events_.signals.empty());
    EXPECT_EQ(events_.signals.back().state, cp::SignalState::kReceiving);
}

TEST_F(HostPeerLinksTest, MediaBeforeAdmissionIsDropped) {
    auto client = make_client();
    const auto peer = connect(*client);
    for (int i = 0; i < 20; ++i) {
        deliver(kClientAddress, client->media_packet(1));
    }
    admit_audible(peer, "ch-1");
    links_.apply_send_mute(peer, false, 1);
    wake();
    float peak = 0.0F;
    for (int i = 0; i < 20; ++i) {
        peak = std::max(peak, render_peak(tp::kSampleRate / 200));
    }
    EXPECT_EQ(peak, 0.0F);
}

TEST_F(HostPeerLinksTest, FramesStillMutedAreNotPlayed) {
    auto client = make_client();
    const auto peer = connect(*client);
    admit_audible(peer, "ch-1");
    // 子機はまだ送信ミュート中（世代 0 のまま）。
    for (int i = 0; i < 20; ++i) {
        deliver(kClientAddress, client->media_packet(0));
    }
    float peak = 0.0F;
    for (int i = 0; i < 20; ++i) {
        peak = std::max(peak, render_peak(tp::kSampleRate / 200));
    }
    EXPECT_EQ(peak, 0.0F);
    EXPECT_TRUE(events_.signals.empty());
}

TEST_F(HostPeerLinksTest, ReportsNoSignalWhenFramesStop) {
    auto client = make_client();
    const auto peer = connect(*client);
    admit_audible(peer, "ch-1");
    links_.apply_send_mute(peer, false, 1);
    deliver(kClientAddress, client->media_packet(1));
    clock_.advance(600ms);
    // コントロールスレッドは周期ごとに受信スレッドを起こす。
    endpoint_.sent.clear();
    links_.poll();
    ASSERT_EQ(endpoint_.sent.size(), 1U);
    EXPECT_EQ(endpoint_.sent[0].to, net::UdpAddress::from_ipv4({127, 0, 0, 1}, endpoint_.port));
    deliver(endpoint_.sent[0].to, endpoint_.sent[0].datagram);
    ASSERT_FALSE(events_.signals.empty());
    EXPECT_EQ(events_.signals.back().peer, peer);
    EXPECT_EQ(events_.signals.back().state, cp::SignalState::kNoSignal);
}

TEST_F(HostPeerLinksTest, ReportsTimeoutAndResume) {
    auto client = make_client();
    const auto peer = connect(*client);
    clock_.advance(6s);
    links_.poll();
    EXPECT_EQ(events_.log, (std::vector<std::string>{"timeout:" + std::to_string(peer)}));
    deliver(kClientAddress, client->control_packet(std::vector<std::uint8_t>{1}));
    links_.poll();
    EXPECT_EQ(events_.log.back(), "resumed:" + std::to_string(peer));
}

TEST_F(HostPeerLinksTest, ClosedConnectionIgnoresFurtherPackets) {
    auto client = make_client();
    const auto peer = connect(*client);
    links_.close(peer);
    EXPECT_EQ(links_.peer_count(), 0U);
    deliver(kClientAddress, client->control_packet(std::vector<std::uint8_t>{1}));
    EXPECT_TRUE(events_.messages.empty());
    // session が閉じた接続は、session に閉じたと知らせ直さない。
    EXPECT_TRUE(events_.log.empty());
}

TEST_F(HostPeerLinksTest, FollowsClientToNewSourceAddress) {
    auto client = make_client();
    const auto peer = connect(*client);
    deliver(kRebound, client->control_packet(std::vector<std::uint8_t>{1}));
    links_.send(peer, std::vector<std::uint8_t>{2});
    ASSERT_FALSE(endpoint_.sent.empty());
    EXPECT_EQ(endpoint_.sent.back().to, kRebound);
}

TEST_F(HostPeerLinksTest, NewHandshakeFromSameAddressClosesUnadmittedConnection) {
    auto first = make_client();
    const auto first_peer = connect(*first);
    auto second = make_client();
    connect(*second);
    EXPECT_EQ(events_.log, (std::vector<std::string>{"closed:" + std::to_string(first_peer)}));
    EXPECT_EQ(links_.peer_count(), 1U);
}

TEST_F(HostPeerLinksTest, NewHandshakeKeepsAdmittedConnectionUntilSessionCloses) {
    auto first = make_client();
    const auto first_peer = connect(*first);
    admit_audible(first_peer, "ch-1");
    events_.log.clear();
    auto second = make_client();
    const auto second_peer = connect(*second);
    EXPECT_TRUE(events_.log.empty());
    EXPECT_EQ(links_.peer_count(), 2U);

    // 再接続: session は新しい接続を同じストリップで承認してから、古い接続を閉じる。
    links_.admit(second_peer, mic_to("ch-1"));
    links_.close(first_peer);
    links_.apply_send_mute(second_peer, false, 1);
    for (int i = 0; i < 20; ++i) {
        deliver(kClientAddress, second->media_packet(1));
    }
    float peak = 0.0F;
    for (int i = 0; i < 20; ++i) {
        peak = std::max(peak, render_peak(tp::kSampleRate / 200));
    }
    // 古い接続を閉じても、新しい接続のストリームはストリップに残る。
    EXPECT_GT(peak, 0.05F);
}

TEST_F(HostPeerLinksTest, AdmissionToMissingStripFailsThePath) {
    auto client = make_client();
    const auto peer = connect(*client);
    links_.admit(peer, mic_to("no-such-strip"));
    links_.poll();
    EXPECT_EQ(events_.log, (std::vector<std::string>{"path_failed:" + std::to_string(peer)}));
}

TEST_F(HostPeerLinksTest, RoutesEachStreamToItsStrip) {
    auto client = make_client();
    const auto peer = connect(*client);
    engine_mixer_.add_channel("ch-1");
    engine_mixer_.add_channel("ch-2");
    // マイク（モノラル）は ch-1、ストリーム 1（ステレオ）は ch-2 へ。聞こえるのは ch-2 だけにする。
    links_.admit(peer, {session::StreamRoute{.stream_id = 0, .channels = 1, .strip_id = "ch-1"},
                        session::StreamRoute{.stream_id = 1, .channels = 2, .strip_id = "ch-2"}});
    links_.poll();
    EXPECT_EQ(events_.log, (std::vector<std::string>{"established:1"}));
    engine_mixer_.set_channel_muted("ch-2", false);
    engine_mixer_.set_master_muted(false);
    links_.apply_send_mute(peer, false, 1);
    // ch-1 のストリームと、対応のないストリーム 7 は ch-2 から聞こえない。
    EXPECT_EQ(stream_for_a_while(*client, 0, 1), 0.0F);
    EXPECT_EQ(stream_for_a_while(*client, 7, 1), 0.0F);
    EXPECT_GT(stream_for_a_while(*client, 1, 2), 0.05F);
}

TEST_F(HostPeerLinksTest, ReplacingRoutesMovesStreamAndReportsEstablishedOnce) {
    auto client = make_client();
    const auto peer = connect(*client);
    engine_mixer_.add_channel("ch-1");
    engine_mixer_.add_channel("ch-2");
    links_.admit(peer, mic_to("ch-1"));
    links_.poll();
    // 申告の変更で、マイクを ch-2 へ移した。
    links_.admit(peer, mic_to("ch-2"));
    links_.poll();
    EXPECT_EQ(events_.log, (std::vector<std::string>{"established:1"}));
    engine_mixer_.set_master_muted(false);
    links_.apply_send_mute(peer, false, 1);
    // ch-1 からは外れたので聞こえない。
    engine_mixer_.set_channel_muted("ch-1", false);
    EXPECT_EQ(stream_for_a_while(*client, 0, 1), 0.0F);
    engine_mixer_.set_channel_muted("ch-1", true);
    engine_mixer_.set_channel_muted("ch-2", false);
    EXPECT_GT(stream_for_a_while(*client, 0, 1), 0.05F);
}

TEST_F(HostPeerLinksTest, DoesNotWakeReceiveThreadWithoutConnections) {
    links_.poll();
    EXPECT_TRUE(endpoint_.sent.empty());
}

}  // namespace
}  // namespace vocroz::engine

namespace vocroz::engine {
namespace {

TEST_F(HostPeerLinksTest, MidiOfAdmittedClientGoesToItsFirstStrip) {
    auto client = make_client();
    const auto peer = connect(*client);
    engine_mixer_.add_channel("ch-1");
    engine_mixer_.add_channel("ch-2");
    links_.admit(peer, {session::StreamRoute{.stream_id = 0, .channels = 1, .strip_id = "ch-1"},
                        session::StreamRoute{.stream_id = 1, .channels = 1, .strip_id = "ch-2"}});
    links_.poll();
    deliver(kClientAddress, client->midi_packet(kNoteOn));

    std::vector<ac::MidiEvent> events;
    pull_midi("ch-1", events);
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events[0].data[0], 0x90);
    EXPECT_EQ(events[0].data[1], 60);
    EXPECT_EQ(midi_inbox_.queue_for("ch-2"), nullptr);
    EXPECT_EQ(midi_inbox_.rejected_count(), 0U);
}

TEST_F(HostPeerLinksTest, MidiBeforeAdmissionIsDropped) {
    auto client = make_client();
    const auto peer = connect(*client);
    deliver(kClientAddress, client->midi_packet(kNoteOn));
    EXPECT_EQ(midi_inbox_.rejected_count(), 1U);

    engine_mixer_.add_channel("ch-1");
    links_.admit(peer, mic_to("ch-1"));
    std::vector<ac::MidiEvent> events;
    pull_midi("ch-1", events);
    EXPECT_TRUE(events.empty());
}

TEST_F(HostPeerLinksTest, AdmittedMidiIsAGraphSourceThatTheMixerDrains) {
    auto client = make_client();
    const auto peer = connect(*client);
    engine_mixer_.add_channel("ch-1");
    links_.admit(peer, mic_to("ch-1"));
    deliver(kClientAddress, client->midi_packet(kNoteOn));
    static_cast<void>(render_peak(ac::kBlockSize));
    std::vector<ac::MidiEvent> events;
    pull_midi("ch-1", events);
    EXPECT_TRUE(events.empty());
}

TEST_F(HostPeerLinksTest, ClosingStopsTheNotesOfTheClient) {
    auto client = make_client();
    const auto peer = connect(*client);
    engine_mixer_.add_channel("ch-1");
    links_.admit(peer, mic_to("ch-1"));
    deliver(kClientAddress, client->midi_packet(kNoteOn));
    links_.close(peer);
    std::vector<ac::MidiEvent> events;
    EXPECT_TRUE(pull_midi("ch-1", events).panic);
}

}  // namespace
}  // namespace vocroz::engine
