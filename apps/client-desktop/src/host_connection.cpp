// SPDX-License-Identifier: AGPL-3.0-only
//
// HostConnection の実装。

#include "host_connection.h"

#include <algorithm>
#include <type_traits>
#include <utility>

#include "vocroz/network/address_selection.h"
#include "vocroz/transport/control_lane.h"
#include "vocroz/transport/packet_header.h"

namespace vocroz::client {
namespace {

transport::PublicKey to_public_key(const session::HostPublicKey& key) {
    transport::PublicKey out{};
    std::copy(key.begin(), key.end(), out.begin());
    return out;
}

network::RendezvousId to_rendezvous_id(const session::RendezvousId& id) {
    network::RendezvousId out{};
    std::copy(id.begin(), id.end(), out.begin());
    return out;
}

}  // namespace

// 確立した 1 つの接続の信頼制御層と死活監視。張り直すたびに作り直す。
// 送信方向（PacketSealer とソケット）は送信スレッドが、受信方向の復号器は受信スレッドが持ち、
// ここには送信先の更新に使うソケットへの参照と、受信スレッドへ渡した接続の番号だけを持つ。
struct HostConnection::Link {
    Link(std::uint32_t generation_in, network::PeerDatagramSocket& socket_in, HostConnection& owner)
        : generation(generation_in),
          socket(socket_in),
          control(owner.ports_.clock, owner, owner),
          liveness(owner.ports_.clock, owner),
          authenticated(owner.ports_.path.authenticated_packets()) {}

    std::uint32_t generation;
    // 送信スレッドが持つ SendPath の中のソケット。この Link がある間は、送信スレッドは
    // 次の set_path() まで破棄しない。
    network::PeerDatagramSocket& socket;
    transport::ReliableControlChannel control;
    transport::LivenessMonitor liveness;
    // 最後に見た、受信スレッドが認証したパケットの数。増えていれば受信があった。
    std::uint64_t authenticated;
};

HostConnection::HostConnection(Ports ports, std::string server_host, std::uint16_t server_port)
    : ports_(ports),
      server_host_(std::move(server_host)),
      server_port_(server_port),
      rendezvous_(ports.endpoint, ports.random, *this),
      backoff_(ports.random) {}

HostConnection::~HostConnection() {
    // 送信スレッドが古い鍵で送り続けないよう、送信先を外してから破棄する。
    // 受信スレッドの復号器も手放させる（受信スレッドはこのオブジェクトより長く動くことがある）。
    if (link_) {
        static_cast<void>(ports_.path.detach(link_->generation));
    }
    ports_.sender.set_path(nullptr);
}

void HostConnection::connect(const session::HostEndpoint& endpoint) {
    target_ = endpoint;
    phase_ = Phase::kConnecting;
    request_ = Request::kStart;
    backoff_.reset();
}

void HostConnection::reconnect() {
    if (!target_) {
        return;
    }
    phase_ = Phase::kReconnectWait;
    request_ = Request::kDropLink;
    retry_at_ = ports_.clock.now() + backoff_.next_delay();
}

void HostConnection::send(std::span<const std::uint8_t> message) {
    if (!link_ || request_ != Request::kNone) {
        // 接続がないか、捨てることが決まっている。session は接続のたびに状態を送り直す。
        return;
    }
    if (!link_->control.send(transport::ControlLane::kControl, message)) {
        ports_.logger.log(common::LogLevel::kWarning,
                          "client: a control message to the host could not be queued");
    }
}

void HostConnection::apply_return_mute(bool muted, std::uint32_t epoch) {
    // 返送のフレームの受け入れは受信スレッドで判定するので、通知をそこへ渡す。
    // 通知は今の接続で届いたものだけで、次の接続では親機が承認のあとに送り直す。
    if (!link_ || !ports_.path.set_return_mute(link_->generation, muted, epoch)) {
        ports_.logger.log(common::LogLevel::kWarning,
                          "client: the return mute could not be passed to the receive thread");
    }
}

void HostConnection::close() {
    target_.reset();
    phase_ = Phase::kIdle;
    request_ = Request::kDropLink;
}

void HostConnection::poll() {
    apply_request();
    const auto now = ports_.clock.now();
    if (phase_ == Phase::kReconnectWait && now >= retry_at_) {
        phase_ = Phase::kReconnecting;
        start_attempt();
    }
    while (const auto message = ports_.router.pop_rendezvous()) {
        if (rendezvous_.active()) {
            rendezvous_.on_rendezvous_datagram(*message, now);
        }
    }
    if (const auto deadline = rendezvous_.next_deadline(); deadline && now >= *deadline) {
        rendezvous_.poll(now);
    }
    while (const auto datagram = ports_.inbox.pop()) {
        handle_handshake_response(*datagram);
    }
    while (const auto event = ports_.path.pop_event()) {
        handle_path_event(*event);
    }
    if (link_) {
        if (const auto authenticated = ports_.path.authenticated_packets();
            authenticated != link_->authenticated) {
            link_->authenticated = authenticated;
            link_->liveness.note_packet_received();
        }
        if (now >= link_->control.next_deadline()) {
            link_->control.tick();
        }
        link_->liveness.poll();
    }
    dispatch_events();
}

void HostConnection::apply_request() {
    const Request request = std::exchange(request_, Request::kNone);
    switch (request) {
        case Request::kNone:
            return;
        case Request::kStart:
            drop_link();
            start_attempt();
            return;
        case Request::kDropLink:
            drop_link();
            return;
    }
}

void HostConnection::drop_link() {
    rendezvous_.stop();
    handshake_.reset();
    if (link_) {
        if (!ports_.path.detach(link_->generation)) {
            // 受信スレッドは次の接続を渡したときに古い復号器を手放すので、受信は止まらないだけ。
            ports_.logger.log(common::LogLevel::kWarning,
                              "client: the receive thread could not be told to drop the link");
        }
        link_.reset();
        ports_.sender.set_path(nullptr);
    }
}

void HostConnection::start_attempt() {
    if (!target_) {
        return;
    }
    const auto resolved = ports_.environment.resolve(server_host_, server_port_);
    const auto server = resolved ? network::select_server_address(resolved.value()) : std::nullopt;
    if (!server) {
        ports_.logger.log(common::LogLevel::kWarning,
                          "client: the rendezvous server name could not be resolved");
        attempt_failed();
        return;
    }
    const auto connection_id = transport::generate_connection_id();
    if (!connection_id) {
        attempt_failed();
        return;
    }
    auto initiator = transport::NoiseIkHandshake::create_initiator(
        ports_.keys, to_public_key(target_->host_public_key), connection_id.value());
    if (!initiator) {
        attempt_failed();
        return;
    }
    network::ClientRendezvousRequest request;
    request.server = *server;
    request.rendezvous_id = to_rendezvous_id(target_->rendezvous_id);
    request.local_candidates = network::select_local_candidates(
        ports_.environment.interface_addresses(), ports_.endpoint.local_port(), std::nullopt);
    request.opening_datagram.assign(transport::kMaxPacketBytes, 0);
    // 参加の申請は鍵交換の後に制御メッセージで送るので、1 つ目のメッセージのペイロードは空にする。
    const auto written = initiator.value().write_initiation({}, request.opening_datagram);
    if (!written) {
        attempt_failed();
        return;
    }
    request.opening_datagram.resize(written.value());
    handshake_ = std::move(initiator).value();
    rendezvous_.start(request, ports_.clock.now());
}

void HostConnection::attempt_failed() {
    rendezvous_.stop();
    handshake_.reset();
    switch (phase_) {
        case Phase::kConnecting:
            phase_ = Phase::kIdle;
            // NAT 越えの失敗、ランデブーサーバーに届かない、親機が登録していない、のどれも、
            // 出演者にできることは同じ（回線と招待を確かめる）なので UNREACHABLE にまとめる。
            events_.emplace_back(Failed{control_protocol::ErrorCode::kUnreachable});
            return;
        case Phase::kReconnecting:
        case Phase::kReconnectWait:
            phase_ = Phase::kReconnectWait;
            retry_at_ = ports_.clock.now() + backoff_.next_delay();
            return;
        case Phase::kIdle:
        case Phase::kLinked:
            return;
    }
}

void HostConnection::on_path_failed(network::NetworkError cause) {
    ports_.logger.log(common::LogLevel::kInfo,
                      cause == network::NetworkError::kHostNotFound
                          ? "client: the host is not registered at the rendezvous server"
                          : "client: no path to the host could be established");
    attempt_failed();
}

void HostConnection::handle_handshake_response(const PeerDatagram& datagram) {
    const auto header = transport::parse_packet_header(datagram.view());
    if (!header || header.value().type != transport::PacketType::kHandshakeResponse) {
        return;
    }
    if (!handshake_) {
        // 複数の経路から届いた同じ応答の 2 つ目以降か、古い試行への応答。
        return;
    }
    std::vector<std::uint8_t> payload(datagram.size);
    if (!handshake_->read_response(datagram.view(), payload)) {
        // 偽造か別の接続への応答。今の試行は続ける。
        return;
    }
    auto secure = handshake_->finish();
    handshake_.reset();
    rendezvous_.mark_established();
    if (!secure) {
        attempt_failed();
        return;
    }
    const std::uint32_t generation = ++generation_;
    if (!ports_.path.attach(generation, std::move(secure.value().opener), datagram.from)) {
        ports_.logger.log(common::LogLevel::kWarning,
                          "client: the receive thread could not take the new link");
        attempt_failed();
        return;
    }
    auto socket = std::make_unique<network::PeerDatagramSocket>(ports_.endpoint, datagram.from);
    network::PeerDatagramSocket& socket_ref = *socket;
    ports_.sender.set_path(std::make_unique<SendPath>(
        SendPath{std::move(secure.value().sealer), std::move(socket), 0}));
    link_ = std::make_unique<Link>(generation, socket_ref, *this);
    phase_ = Phase::kLinked;
    backoff_.reset();
    events_.emplace_back(Established{});
}

void HostConnection::handle_path_event(const HostPacketEvent& event) {
    if (!link_ || event.generation != link_->generation) {
        return;  // 捨てた接続の出来事
    }
    switch (event.kind) {
        case HostPacketEvent::Kind::kControl:
            if (!link_->control.on_control_packet(event.view())) {
                ports_.logger.log(common::LogLevel::kDebug,
                                  "client: a malformed control packet was dropped");
            }
            return;
        case HostPacketEvent::Kind::kRemoteChanged:
            // NAT のマッピングが変わった。認証済みのパケットの送信元へ送り先を移す。
            if (!link_->socket.update_remote(event.from)) {
                ports_.logger.log(common::LogLevel::kWarning,
                                  "client: the new host address could not be passed to the sender");
            }
            return;
    }
}

void HostConnection::on_control_message(transport::ControlLane lane,
                                        std::span<const std::uint8_t> message) {
    if (lane != transport::ControlLane::kControl) {
        return;
    }
    events_.emplace_back(Message{{message.begin(), message.end()}});
}

common::Result<void, transport::TransportError> HostConnection::send_control_packet(
    std::span<const std::uint8_t> plaintext) noexcept {
    if (!ports_.sender.push_control(plaintext)) {
        return common::make_unexpected(transport::TransportError::kSendFailed);
    }
    return {};
}

void HostConnection::on_peer_timed_out() {
    events_.emplace_back(Lost{});
}

void HostConnection::on_peer_resumed() {
    events_.emplace_back(Resumed{});
}

void HostConnection::dispatch_events() {
    // 通知の中で新しい通知が溜まることはない（session からの要求は次の poll() で反映する）が、
    // 念のため取り出してから配る。
    auto events = std::move(events_);
    events_.clear();
    for (auto& event : events) {
        std::visit(
            [this](auto& e) {
                using E = std::decay_t<decltype(e)>;
                if constexpr (std::is_same_v<E, Established>) {
                    ports_.listener.on_link_established();
                } else if constexpr (std::is_same_v<E, Message>) {
                    ports_.listener.on_link_message(e.bytes);
                } else if constexpr (std::is_same_v<E, Lost>) {
                    ports_.listener.on_link_lost();
                } else if constexpr (std::is_same_v<E, Resumed>) {
                    ports_.listener.on_link_resumed();
                } else if constexpr (std::is_same_v<E, Failed>) {
                    ports_.listener.on_link_failed(e.reason);
                }
            },
            event);
    }
}

}  // namespace vocroz::client
