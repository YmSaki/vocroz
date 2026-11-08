// SPDX-License-Identifier: AGPL-3.0-only
//
// ChannelRelay の実装。

#include "vocroz/control_protocol/channel_relay.h"

#include <utility>

namespace vocroz::control_protocol {

ChannelRelay::ChannelRelay(MessageChannel& ui, MessageChannel& engine, ClosedHandler on_closed)
    : ui_(ui),
      engine_(engine),
      on_closed_(std::move(on_closed)),
      from_ui_(engine, Side::kUi, on_closed_),
      from_engine_(ui, Side::kEngine, on_closed_) {
    ui_.set_listener(&from_ui_);
    engine_.set_listener(&from_engine_);
}

ChannelRelay::~ChannelRelay() {
    ui_.set_listener(nullptr);
    engine_.set_listener(nullptr);
}

ChannelRelay::Forwarder::Forwarder(MessageChannel& destination, Side side,
                                   const ClosedHandler& on_closed)
    : destination_(destination), side_(side), on_closed_(on_closed) {}

void ChannelRelay::Forwarder::on_message(std::string_view message) {
    // 送れなかったメッセージは捨てる。相手の経路が閉じていればその経路の on_closed()
    // で知らせるので、
    // ここで再送や通知はしない。大きすぎるメッセージは、両側が同じ上限を使うので受信の時点で弾かれている。
    (void)destination_.send(message);
}

void ChannelRelay::Forwarder::on_closed() {
    if (on_closed_) {
        on_closed_(side_);
    }
}

}  // namespace vocroz::control_protocol
