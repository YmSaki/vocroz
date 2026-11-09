// SPDX-License-Identifier: AGPL-3.0-only
//
// ランデブーサーバーへの登録。

#include "rendezvous_registration.h"

#include <array>
#include <utility>

#include "network_constants.h"

namespace vocroz::network {

RendezvousRegistration::RendezvousRegistration(UdpEndpoint& endpoint, Listener& listener) noexcept
    : endpoint_(endpoint), listener_(listener) {}

void RendezvousRegistration::start(RegistrationConfig config, const wire::TxId& tx_id,
                                   common::Clock::TimePoint now) {
    config_ = std::move(config);
    tx_id_ = tx_id;
    started_at_ = now;
    last_registered_at_.reset();
    send_register();
    next_send_at_ = now + kRegisterRetryInterval;
}

void RendezvousRegistration::stop() noexcept {
    config_.reset();
    last_registered_at_.reset();
}

void RendezvousRegistration::set_candidates(std::vector<UdpAddress> candidates) {
    if (config_) {
        config_->candidates = std::move(candidates);
    }
}

void RendezvousRegistration::poll(common::Clock::TimePoint now) {
    if (!config_ || now < next_send_at_) {
        return;
    }
    send_register();
    // 登録できるまでは短い間隔で送り直し、登録できた後は寿命に間に合う間隔に延ばす。
    const auto interval = last_registered_at_ && config_->refresh_interval > kRegisterRetryInterval
                              ? config_->refresh_interval
                              : kRegisterRetryInterval;
    next_send_at_ = now + interval;
}

void RendezvousRegistration::on_datagram(const RendezvousDatagram& datagram,
                                         common::Clock::TimePoint now) {
    if (!config_ || datagram.from != config_->server) {
        return;
    }
    const auto bytes = datagram.view();
    if (auto registered = wire::parse_registered(bytes)) {
        if (registered.value().tx_id != tx_id_) {
            return;
        }
        if (!registered.value().capacity_exceeded) {
            last_registered_at_ = now;
        }
        listener_.on_registered(registered.value());
        return;
    }
    auto punch = wire::parse_punch(bytes);
    if (!punch) {
        return;
    }
    // 登録者が受け取る PUNCH は自分の txid、相手側として受け取る PUNCH は txid が 0。
    constexpr wire::TxId kZeroTxId{};
    const auto& message = punch.value();
    if ((message.tx_id != tx_id_ && message.tx_id != kZeroTxId) ||
        message.rendezvous_id != config_->rendezvous_id || message.peer_role == config_->role) {
        return;
    }
    listener_.on_punch(message);
}

std::optional<common::Clock::TimePoint> RendezvousRegistration::next_deadline() const noexcept {
    if (!config_) {
        return std::nullopt;
    }
    return next_send_at_;
}

void RendezvousRegistration::send_register() {
    std::array<std::uint8_t, wire::kMaxRegisterBytes> buffer{};
    const wire::RegisterMessage message{tx_id_, config_->rendezvous_id, config_->peer_id,
                                        config_->role, config_->candidates};
    const auto written = wire::encode_register(message, buffer);
    if (!written) {
        return;  // 候補は select_local_candidates() で選ぶので、ここに来るのは呼び出し側の誤りだけ
    }
    // 送れなくても次の送り直しで回復するので、結果は確かめない。
    (void)endpoint_.send_to(config_->server,
                            std::span<const std::uint8_t>(buffer).first(written.value()));
}

}  // namespace vocroz::network
