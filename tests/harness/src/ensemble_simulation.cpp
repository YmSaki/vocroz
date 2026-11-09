// SPDX-License-Identifier: AGPL-3.0-only
//
// EnsembleSimulation の実装。

#include "vocroz/test_harness/ensemble_simulation.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <numeric>
#include <utility>
#include <variant>

#include "vocroz/control_protocol/events.h"
#include "vocroz/session/invite_code.h"
#include "vocroz/session/ports/rendezvous_client.h"
#include "vocroz/transport/constants.h"

namespace vocroz::test_harness {
namespace {

namespace cp = control_protocol;

// 1 回に進める時間の上限（サンプル数）。48 kHz で 0.5 ms。
// ジッターバッファとネットワークの配送の時刻の分解能を十分に細かく保つ。
constexpr std::uint64_t kMaxStepSamples = 24;

// ランデブーサーバーが返す招待コードと失効の鍵（代わりの実装なので固定値でよい）。
constexpr std::string_view kInviteCode = "ABCDE12345";
constexpr std::uint8_t kRevokeKeyByte = 0x5A;

// 子機のアドレスの先頭（子機 index は host = kFirstClientHost + index）。
constexpr std::uint32_t kFirstClientHost = 10;
constexpr std::uint16_t kClientPort = 50000;

common::Clock::TimePoint::duration samples_to_duration(std::uint64_t samples) {
    return std::chrono::duration_cast<common::Clock::TimePoint::duration>(
        std::chrono::nanoseconds{samples * 1'000'000'000ULL / transport::kSampleRate});
}

session::InviteCode invite_code() {
    auto code = session::parse_invite_code(kInviteCode);
    if (!code) {
        std::abort();
    }
    return std::move(code).value();
}

// UI に返った応答から、request_id の Ack か Reject を探す。
common::Result<cp::Revision, cp::ErrorCode> find_reply(const session::FakeUiEventSink& ui,
                                                       const std::string& request_id) {
    for (auto it = ui.messages().rbegin(); it != ui.messages().rend(); ++it) {
        if (const auto* ack = std::get_if<cp::Ack>(&*it); ack && ack->request_id == request_id) {
            return ack->applied_revision;
        }
        if (const auto* rej = std::get_if<cp::Reject>(&*it); rej && rej->request_id == request_id) {
            return common::make_unexpected(rej->error_code);
        }
    }
    return common::make_unexpected(cp::ErrorCode::kEngineError);
}

cp::Revision revision_of(const cp::MixerSnapshot& snapshot, const std::string& entity_id) {
    if (entity_id == snapshot.session.entity_id) {
        return snapshot.session.revision;
    }
    if (entity_id == snapshot.master.entity_id) {
        return snapshot.master.revision;
    }
    for (const auto& c : snapshot.channels) {
        if (c.entity_id == entity_id) {
            return c.revision;
        }
    }
    for (const auto& p : snapshot.pending) {
        if (p.entity_id == entity_id) {
            return p.revision;
        }
    }
    return 0;
}

}  // namespace

EnsembleSimulation::EnsembleSimulation(EnsembleConfig config)
    : config_(std::move(config)),
      step_samples_(std::gcd(config_.block, static_cast<std::size_t>(kMaxStepSamples))),
      capture_buffer_(config_.block),
      output_buffer_(config_.block) {
    host_ = std::make_unique<SimulatedHost>(clock_, network_, kHostAddress, config_.playout);
    for (std::size_t i = 0; i < config_.client_names.size(); ++i) {
        const netsim::NodeAddress address{.host = kFirstClientHost + static_cast<std::uint32_t>(i),
                                          .port = kClientPort};
        SimulatedClientConfig client_config;
        client_config.display_name = config_.client_names[i];
        client_config.encoder = config_.encoder;
        client_config.ignore_send_mute =
            i < config_.ignore_send_mute.size() && config_.ignore_send_mute[i];
        clients_.push_back(std::make_unique<SimulatedClient>(clock_, network_, address,
                                                             kHostAddress, client_config));
        network_.set_fault_profile(address, kHostAddress, config_.link);
        network_.set_fault_profile(kHostAddress, address, config_.link);
    }
    served_resolutions_.assign(clients_.size(), 0);
}

EnsembleSimulation::~EnsembleSimulation() {
    // 子機を親機より先に壊す（どちらもネットワークから外れるだけなので順序の制約はないが、
    // 生成の逆順にそろえる）。
    clients_.clear();
    host_.reset();
}

void EnsembleSimulation::run(std::uint64_t samples, const Source& source, const Sink& sink) {
    const std::uint64_t end = now_samples_ + samples;
    while (now_samples_ < end) {
        const std::uint64_t t = now_samples_ + step_samples_;
        advance_clock_to(t);
        if (t % config_.block == 0) {
            // キャプチャのコールバックは、ブロックを録り終えた時刻に呼ばれる。
            for (std::size_t i = 0; i < clients_.size(); ++i) {
                std::fill(capture_buffer_.begin(), capture_buffer_.end(), 0.0F);
                if (source) {
                    source(i, t - config_.block, capture_buffer_);
                }
                clients_[i]->capture(capture_buffer_);
            }
        }
        network_.deliver_due();
        host_->step();
        for (auto& client : clients_) {
            client->step();
        }
        serve_rendezvous();
        if (t % config_.block == 0) {
            // 出力のコールバックは、ブロックを鳴らし始める時刻に呼ばれる。
            host_->render(output_buffer_);
            if (sink) {
                sink(t, output_buffer_);
            }
        }
    }
}

void EnsembleSimulation::run_for(std::chrono::milliseconds duration) {
    run(static_cast<std::uint64_t>(duration.count()) * transport::kSampleRate / 1000);
}

common::Result<cp::Revision, cp::ErrorCode> EnsembleSimulation::host_command(
    const std::string& entity_id, cp::DiscreteCommandBody body) {
    const auto request_id = "h" + std::to_string(++request_number_);
    auto& session = host_->session();
    session.handle_ui_message(cp::DiscreteCommand{
        request_id, entity_id, revision_of(session.snapshot(), entity_id), std::move(body)});
    serve_rendezvous();
    return find_reply(host_->ui(), request_id);
}

common::Result<cp::Revision, cp::ErrorCode> EnsembleSimulation::client_command(
    std::size_t index, cp::DiscreteCommandBody body) {
    const auto request_id = "c" + std::to_string(++request_number_);
    auto& client = *clients_.at(index);
    const auto snapshot = client.session().snapshot();
    client.session().handle_ui_message(
        cp::DiscreteCommand{request_id, snapshot.entity_id, snapshot.revision, std::move(body)});
    serve_rendezvous();
    return find_reply(client.ui(), request_id);
}

void EnsembleSimulation::disconnect(std::size_t index) {
    const auto& address = clients_.at(index)->address();
    network_.begin_outage(address, kHostAddress, netsim::OutageMode::kDrop);
    network_.begin_outage(kHostAddress, address, netsim::OutageMode::kDrop);
}

void EnsembleSimulation::restore(std::size_t index) {
    const auto& address = clients_.at(index)->address();
    network_.end_outage(address, kHostAddress);
    network_.end_outage(kHostAddress, address);
}

std::optional<std::string> EnsembleSimulation::invite() const {
    return host_->session().snapshot().session.invite_url;
}

void EnsembleSimulation::advance_clock_to(std::uint64_t samples) {
    clock_.advance(samples_to_duration(samples) - samples_to_duration(now_samples_));
    now_samples_ = samples;
}

// 招待の登録と解決に、ランデブーサーバーの代わりにその場で答える。
// 親機の登録した招待トークンとランデブー ID を、子機の解決の結果としてそのまま返す。
void EnsembleSimulation::serve_rendezvous() {
    const auto& registrations = host_->rendezvous().registrations();
    for (; served_registrations_ < registrations.size(); ++served_registrations_) {
        session::RevokeKey key{};
        key.fill(kRevokeKeyByte);
        host_->session().on_invite_registered(registrations[served_registrations_].invite_token,
                                              session::RegisteredInvite{invite_code(), key});
    }
    for (std::size_t i = 0; i < clients_.size(); ++i) {
        const auto& resolutions = clients_[i]->rendezvous().resolutions();
        for (; served_resolutions_[i] < resolutions.size(); ++served_resolutions_[i]) {
            const auto& code = resolutions[served_resolutions_[i]];
            if (registrations.empty() || code != invite_code()) {
                clients_[i]->session().on_invite_resolved(
                    code, common::make_unexpected(cp::ErrorCode::kInviteInvalid));
                continue;
            }
            const auto& latest = registrations.back();
            session::ResolvedInvite resolved;
            resolved.endpoint.host_public_key = latest.host_public_key;
            resolved.endpoint.rendezvous_id = latest.rendezvous_id;
            resolved.invite_token = latest.invite_token;
            clients_[i]->session().on_invite_resolved(code, resolved);
        }
    }
}

}  // namespace vocroz::test_harness
