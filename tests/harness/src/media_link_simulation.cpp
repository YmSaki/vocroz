// SPDX-License-Identifier: AGPL-3.0-only
//
// MediaLinkSimulation の実装。

#include "vocroz/test_harness/media_link_simulation.h"

#include <chrono>
#include <numeric>
#include <utility>

#include "vocroz/transport/constants.h"

namespace vocroz::test_harness {
namespace {

// 送信ミュートを解除した世代。初期ミュートから 1 回解除した状態として始める。
constexpr std::uint32_t kUnmutedEpoch = 1;

// 時刻を進める刻みの上限（サンプル数）。0.5ms にして、パケットの到着時刻の丸めを
// ジッターバッファの深さ（10ms 以上）より十分小さく保つ。
constexpr std::size_t kMaxStepSamples = 24;

constexpr transport::ConnectionId kConnectionId = 0x5EED0001;

// サンプル位置を仮想時刻（開始からの経過時間）にする。
common::Clock::TimePoint::duration samples_to_duration(std::uint64_t samples) {
    return std::chrono::duration_cast<common::Clock::TimePoint::duration>(
        std::chrono::nanoseconds{samples * 1'000'000'000ULL / transport::kSampleRate});
}

}  // namespace

common::Result<std::unique_ptr<MediaLinkSimulation>, std::string> MediaLinkSimulation::create(
    const MediaLinkConfig& config) {
    if (config.capture_block == 0 || config.output_block == 0) {
        return common::make_unexpected(std::string("block sizes must be positive"));
    }
    auto link = establish_secure_link(kConnectionId);
    if (!link) {
        return common::make_unexpected(std::string("handshake failed"));
    }
    auto simulation = std::make_unique<MediaLinkSimulation>(config, std::move(link).value());
    auto* const sim = simulation.get();
    const bool uplink = config.direction == MediaDirection::kUplink;
    if (uplink) {
        auto sender = MediaSender::create(sim->link_.client.sealer, sim->client_socket_,
                                          config.encoder, 0, kUnmutedEpoch);
        if (!sender) {
            return common::make_unexpected(std::string("failed to create the encoder"));
        }
        sim->sender_ = std::make_unique<MediaSender>(std::move(sender).value());
    } else {
        auto sender = ReturnSender::create(std::move(sim->link_.host.sealer), sim->host_socket_,
                                           config.encoder, kUnmutedEpoch);
        if (!sender) {
            return common::make_unexpected(std::string("failed to create the encoder"));
        }
        sim->return_sender_ = std::move(sender).value();
    }
    auto receiver =
        MediaReceiver::create(uplink ? sim->link_.host.opener : sim->link_.client.opener,
                              config.playout, sim->clock_.now());
    if (!receiver) {
        return common::make_unexpected(std::string("invalid playout configuration"));
    }
    sim->receiver_ = std::move(receiver).value();
    sim->receiver_->unmute(kUnmutedEpoch, sim->clock_.now());
    auto* const receiver_ptr = sim->receiver_.get();
    auto* const clock_ptr = &sim->clock_;
    sim->network_.attach(sim->receiver_address(),
                         [receiver_ptr, clock_ptr](const netsim::NodeAddress&,
                                                   std::span<const std::uint8_t> datagram) {
                             receiver_ptr->on_datagram(datagram, clock_ptr->now());
                         });
    sim->network_.set_fault_profile(sim->sender_address(), sim->receiver_address(), config.faults);
    return simulation;
}

MediaLinkSimulation::MediaLinkSimulation(const MediaLinkConfig& config, SecureLink link)
    : config_(config),
      link_(std::move(link)),
      step_samples_(std::gcd(std::gcd(config.capture_block, config.output_block), kMaxStepSamples)),
      capture_buffer_(config.capture_block),
      output_buffer_(config.output_block) {}

MediaLinkSimulation::~MediaLinkSimulation() = default;

void MediaLinkSimulation::run(std::uint64_t samples, const Source& source, const Sink& sink) {
    const std::uint64_t end = now_samples_ + samples;
    while (now_samples_ < end) {
        const std::uint64_t t = now_samples_ + step_samples_;
        advance_clock_to(t);
        if (t % config_.capture_block == 0) {
            // キャプチャのコールバックは、ブロックを録り終えた時刻に呼ばれる。
            source(t - config_.capture_block, capture_buffer_);
            if (sender_) {
                sender_->capture(capture_buffer_);
            } else {
                return_sender_->push(capture_buffer_);
            }
        }
        network_.deliver_due();
        receiver_->poll(clock_.now());
        if (t % config_.output_block == 0) {
            // 出力のコールバックは、ブロックを鳴らし始める時刻に呼ばれる。
            receiver_->render(output_buffer_);
            sink(t, output_buffer_);
        }
    }
}

netsim::NodeAddress MediaLinkSimulation::sender_address() const noexcept {
    return config_.direction == MediaDirection::kUplink ? kClientAddress : kHostAddress;
}

netsim::NodeAddress MediaLinkSimulation::receiver_address() const noexcept {
    return config_.direction == MediaDirection::kUplink ? kHostAddress : kClientAddress;
}

std::size_t MediaLinkSimulation::codec_lookahead_samples() const noexcept {
    return sender_ ? sender_->lookahead_samples() : return_sender_->lookahead_samples();
}

void MediaLinkSimulation::advance_clock_to(std::uint64_t samples) {
    clock_.advance(samples_to_duration(samples) - samples_to_duration(now_samples_));
    now_samples_ = samples;
}

}  // namespace vocroz::test_harness
