// SPDX-License-Identifier: AGPL-3.0-only
//
// 子機の、親機への経路の確立。

#include "vocroz/network/client_rendezvous.h"

#include "detail/deadline_utils.h"
#include "detail/random_utils.h"
#include "hole_puncher.h"
#include "network_constants.h"
#include "rendezvous_registration.h"

namespace vocroz::network {

class ClientRendezvous::Impl final : public RendezvousRegistration::Listener {
public:
    Impl(UdpEndpoint& endpoint, transport::RandomSource& random, ClientRendezvousListener& listener)
        : random_(random),
          listener_(listener),
          registration_(endpoint, *this),
          puncher_(endpoint) {}

    void start(const ClientRendezvousRequest& request, common::Clock::TimePoint now) {
        RegistrationConfig config;
        config.server = request.server;
        config.rendezvous_id = request.rendezvous_id;
        // ピア ID
        // は試行ごとに変える。前の試行の登録がサーバーに残っていても、別の子機として扱われるようにする。
        config.peer_id = detail::random_bytes<kRendezvousIdBytes>(random_);
        config.role = RendezvousRole::kClient;
        config.candidates = request.local_candidates;
        config.refresh_interval = kRegisterRetryInterval;
        puncher_.stop_all();
        puncher_.set_datagram(request.opening_datagram);
        registered_ = false;
        punched_ = false;
        give_up_at_ = now + kClientConnectTimeout;
        now_ = now;
        registration_.start(std::move(config), detail::random_bytes<8>(random_), now);
    }

    void on_rendezvous_datagram(const RendezvousDatagram& datagram, common::Clock::TimePoint now) {
        now_ = now;
        registration_.on_datagram(datagram, now);
    }

    void poll(common::Clock::TimePoint now) {
        if (!registration_.active()) {
            return;
        }
        now_ = now;
        // サーバーに届かないことは、親機を待つ時間（10 秒）より早く分かるので、先に打ち切る。
        if (!registered_ && now - registration_.started_at() >= kRendezvousTimeout) {
            fail(NetworkError::kRendezvousUnreachable);
            return;
        }
        if (now >= give_up_at_) {
            fail(punched_ ? NetworkError::kUnreachable : NetworkError::kHostNotFound);
            return;
        }
        registration_.poll(now);
        puncher_.poll(now);
    }

    void stop() noexcept {
        registration_.stop();
        puncher_.stop_all();
    }

    [[nodiscard]] bool active() const noexcept {
        return registration_.active();
    }

    [[nodiscard]] std::optional<common::Clock::TimePoint> next_deadline() const noexcept {
        if (!registration_.active()) {
            return std::nullopt;
        }
        const std::optional<common::Clock::TimePoint> rendezvous_timeout =
            registered_ ? std::nullopt
                        : std::optional(registration_.started_at() + kRendezvousTimeout);
        return detail::earliest({registration_.next_deadline(), puncher_.next_deadline(),
                                 give_up_at_, rendezvous_timeout});
    }

    void on_registered(const wire::RegisteredMessage& message) override {
        if (message.capacity_exceeded) {
            fail(NetworkError::kRendezvousCapacity);
            return;
        }
        registered_ = true;
    }

    void on_punch(const wire::PunchMessage& message) override {
        // 子機に届く PUNCH の相手は親機だけ（RendezvousRegistration が役割を確かめている）。
        punched_ = true;
        puncher_.punch(message.peer_id, message.candidates, now_);
    }

private:
    void fail(NetworkError cause) {
        stop();
        listener_.on_path_failed(cause);
    }

    transport::RandomSource& random_;
    ClientRendezvousListener& listener_;
    RendezvousRegistration registration_;
    HolePuncher puncher_;
    bool registered_ = false;
    bool punched_ = false;
    common::Clock::TimePoint give_up_at_{};
    // RendezvousRegistration::Listener
    // の呼び出しは時刻を受け取らないので、呼び出し元の時刻を覚えておく。
    common::Clock::TimePoint now_{};
};

ClientRendezvous::ClientRendezvous(UdpEndpoint& endpoint, transport::RandomSource& random,
                                   ClientRendezvousListener& listener)
    : impl_(std::make_unique<Impl>(endpoint, random, listener)) {}

ClientRendezvous::~ClientRendezvous() = default;

void ClientRendezvous::start(const ClientRendezvousRequest& request, common::Clock::TimePoint now) {
    impl_->start(request, now);
}

void ClientRendezvous::on_rendezvous_datagram(const RendezvousDatagram& datagram,
                                              common::Clock::TimePoint now) {
    impl_->on_rendezvous_datagram(datagram, now);
}

void ClientRendezvous::poll(common::Clock::TimePoint now) {
    impl_->poll(now);
}

void ClientRendezvous::mark_established() noexcept {
    impl_->stop();
}

void ClientRendezvous::stop() noexcept {
    impl_->stop();
}

bool ClientRendezvous::active() const noexcept {
    return impl_->active();
}

std::optional<common::Clock::TimePoint> ClientRendezvous::next_deadline() const noexcept {
    return impl_->next_deadline();
}

}  // namespace vocroz::network
