// SPDX-License-Identifier: AGPL-3.0-only
//
// WebView のブリッジの MessageChannel の実装。

#include "bridge_channel.h"

#include "vocroz/control_protocol/constants.h"

namespace vocroz::webui {

using control_protocol::ChannelError;
using control_protocol::kMaxMessageBytes;

BridgeChannel::BridgeChannel(UiEventSink& sink) : sink_(sink) {}

common::Result<void, ChannelError> BridgeChannel::send(std::string_view message) {
    if (message.size() > kMaxMessageBytes) {
        return common::make_unexpected(ChannelError::kTooLarge);
    }
    // sink_ の呼び出しはブロックしない（メッセージスレッドへの依頼だけ）ので、排他の中で呼んでも
    // 送信どうしが待たされることはほとんどない。
    const std::lock_guard lock{sink_mutex_};
    if (closed_) {
        return common::make_unexpected(ChannelError::kClosed);
    }
    sink_.post_to_ui(std::string{message});
    return {};
}

void BridgeChannel::set_listener(control_protocol::MessageListener* listener) {
    listener_.set(listener);
}

bool BridgeChannel::receive_from_ui(std::string_view message) {
    if (message.size() > kMaxMessageBytes) {
        return false;
    }
    listener_.deliver_message(message);
    return true;
}

void BridgeChannel::close() {
    const std::lock_guard lock{sink_mutex_};
    closed_ = true;
}

}  // namespace vocroz::webui
