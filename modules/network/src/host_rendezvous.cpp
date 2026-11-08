// SPDX-License-Identifier: AGPL-3.0-only
//
// 親機の、ランデブーサーバーへの登録と子機へのホールパンチ。

#include "vocroz/network/host_rendezvous.h"

#include "detail/deadline_utils.h"
#include "detail/random_utils.h"
#include "hole_puncher.h"
#include "network_constants.h"
#include "rendezvous_registration.h"

namespace vocroz::network {

class HostRendezvous::Impl final : public RendezvousRegistration::Listener {
public:
    Impl(UdpEndpoint& endpoint, transport::RandomSource& random, HostRendezvousListener& listener)
        : random_(random),
          listener_(listener),
          registration_(endpoint, *this),
          puncher_(endpoint) {}

    void start(const HostRendezvousRequest& request, common::Clock::TimePoint now) {
        RegistrationConfig config;
        config.server = request.server;
        config.rendezvous_id = request.rendezvous_id;
        config.peer_id = detail::random_bytes<kRendezvousIdBytes>(random_);
        config.role = RendezvousRole::kHost;
        config.candidates = request.local_candidates;
        config.refresh_interval = kHostRegisterRefreshInterval;
        puncher_.stop_all();
        // 親機は子機より先に鍵交換のメッセージを送れないので、マッピングを開くだけの空のデータグラムを送る。
        puncher_.set_datagram({});
        status_ = Status::kUnknown;
        now_ = now;
        registration_.start(std::move(config), detail::random_bytes<8>(random_), now);
    }

    void set_local_candidates(std::vector<UdpAddress> candidates) {
        registration_.set_candidates(std::move(candidates));
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
        registration_.poll(now);
        puncher_.poll(now);
        if (const auto lost_at = registration_lost_at(); lost_at && now >= *lost_at) {
            set_status(Status::kUnavailable, NetworkError::kRendezvousUnreachable);
        }
    }

    void mark_established(const UdpAddress& peer_address) {
        puncher_.stop_for_address(peer_address);
    }

    void stop() noexcept {
        registration_.stop();
        puncher_.stop_all();
    }

    [[nodiscard]] std::optional<common::Clock::TimePoint> next_deadline() const noexcept {
        if (!registration_.active()) {
            return std::nullopt;
        }
        // すでに登録できない状態なら、同じ判定のために起き続ける必要はない。
        const auto lost_at =
            status_ == Status::kUnavailable ? std::nullopt : registration_lost_at();
        return detail::earliest({registration_.next_deadline(), puncher_.next_deadline(), lost_at});
    }

    void on_registered(const wire::RegisteredMessage& message) override {
        if (message.capacity_exceeded) {
            set_status(Status::kUnavailable, NetworkError::kRendezvousCapacity);
            return;
        }
        set_status(Status::kAvailable, NetworkError::kRendezvousUnreachable);
    }

    void on_punch(const wire::PunchMessage& message) override {
        puncher_.punch(message.peer_id, message.candidates, now_);
    }

private:
    enum class Status {
        kUnknown,
        kAvailable,
        kUnavailable,
    };

    // 登録が切れたとみなす時刻。まだ登録できていなければ最初の REGISTER から kRendezvousTimeout、
    // 登録できていれば最後の REGISTERED からサーバーの登録の寿命。
    [[nodiscard]] std::optional<common::Clock::TimePoint> registration_lost_at() const noexcept {
        if (const auto last = registration_.last_registered_at()) {
            return *last + kHostRegistrationLostAfter;
        }
        return registration_.started_at() + kRendezvousTimeout;
    }

    // 状態が変わったときだけ知らせる。cause は kUnavailable のときだけ使う。
    void set_status(Status status, NetworkError cause) {
        if (status == status_) {
            return;
        }
        status_ = status;
        switch (status) {
            case Status::kAvailable:
                listener_.on_rendezvous_available();
                return;
            case Status::kUnavailable:
                listener_.on_rendezvous_unavailable(cause);
                return;
            case Status::kUnknown:
                return;
        }
    }

    transport::RandomSource& random_;
    HostRendezvousListener& listener_;
    RendezvousRegistration registration_;
    HolePuncher puncher_;
    Status status_ = Status::kUnknown;
    // RendezvousRegistration::Listener
    // の呼び出しは時刻を受け取らないので、呼び出し元の時刻を覚えておく。
    common::Clock::TimePoint now_{};
};

HostRendezvous::HostRendezvous(UdpEndpoint& endpoint, transport::RandomSource& random,
                               HostRendezvousListener& listener)
    : impl_(std::make_unique<Impl>(endpoint, random, listener)) {}

HostRendezvous::~HostRendezvous() = default;

void HostRendezvous::start(const HostRendezvousRequest& request, common::Clock::TimePoint now) {
    impl_->start(request, now);
}

void HostRendezvous::set_local_candidates(std::vector<UdpAddress> candidates) {
    impl_->set_local_candidates(std::move(candidates));
}

void HostRendezvous::on_rendezvous_datagram(const RendezvousDatagram& datagram,
                                            common::Clock::TimePoint now) {
    impl_->on_rendezvous_datagram(datagram, now);
}

void HostRendezvous::poll(common::Clock::TimePoint now) {
    impl_->poll(now);
}

void HostRendezvous::mark_established(const UdpAddress& peer_address) {
    impl_->mark_established(peer_address);
}

void HostRendezvous::stop() noexcept {
    impl_->stop();
}

std::optional<common::Clock::TimePoint> HostRendezvous::next_deadline() const noexcept {
    return impl_->next_deadline();
}

}  // namespace vocroz::network
