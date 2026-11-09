// SPDX-License-Identifier: AGPL-3.0-only
//
// HostRendezvousDriver の実装。

#include "host_rendezvous_driver.h"

#include <algorithm>
#include <utility>

#include "vocroz/network/address_selection.h"

namespace vocroz::engine {

HostRendezvousDriver::HostRendezvousDriver(Ports ports, std::string server_host,
                                           std::uint16_t server_port)
    : ports_(ports),
      server_host_(std::move(server_host)),
      server_port_(server_port),
      rendezvous_(ports.endpoint, ports.random, *this) {}

void HostRendezvousDriver::start(const session::RendezvousId& id) {
    if (current_ == id || server_host_.empty()) {
        return;
    }
    // 名前の解決はブロックするが、セッションの作成時に 1 回だけなので、子機と同じく
    // コントロールスレッドで行う（apps/client-desktop の HostConnection と同じ）。
    const auto resolved = ports_.environment.resolve(server_host_, server_port_);
    const auto server = resolved ? network::select_server_address(resolved.value()) : std::nullopt;
    if (!server) {
        ports_.logger.log(common::LogLevel::kWarning,
                          "ランデブーサーバーの名前を解決できないため、子機を受け付けられません");
        rendezvous_.stop();
        current_.reset();
        return;
    }
    network::HostRendezvousRequest request;
    request.server = *server;
    std::copy(id.begin(), id.end(), request.rendezvous_id.begin());
    // 自動ポート開放の外部アドレスはまだ申告しない（ポート開放は組み立ての次の段階）。
    request.local_candidates = network::select_local_candidates(
        ports_.environment.interface_addresses(), ports_.endpoint.local_port(), std::nullopt);
    rendezvous_.start(request, ports_.clock.now());
    current_ = id;
}

void HostRendezvousDriver::stop() noexcept {
    rendezvous_.stop();
    current_.reset();
}

void HostRendezvousDriver::poll() {
    const auto now = ports_.clock.now();
    while (const auto message = ports_.router.pop_rendezvous()) {
        if (current_) {
            rendezvous_.on_rendezvous_datagram(*message, now);
        }
    }
    if (const auto deadline = rendezvous_.next_deadline(); deadline && now >= *deadline) {
        rendezvous_.poll(now);
    }
}

void HostRendezvousDriver::on_rendezvous_available() {
    ports_.logger.log(common::LogLevel::kInfo, "ランデブーサーバーに登録しました");
}

void HostRendezvousDriver::on_rendezvous_unavailable(network::NetworkError cause) {
    ports_.logger.log(common::LogLevel::kWarning,
                      cause == network::NetworkError::kRendezvousCapacity
                          ? "ランデブーサーバーが上限に達しています。登録を送り直します"
                          : "ランデブーサーバーから応答がありません。登録を送り直します");
}

}  // namespace vocroz::engine
