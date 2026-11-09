// SPDX-License-Identifier: AGPL-3.0-only
//
// ui_event_publisher.h の実装。

#include "ui_event_publisher.h"

#include "vocroz/control_protocol/json_codec.h"

namespace vocroz::engine {

void UiEventPublisher::publish(const control_protocol::EngineToUiMessage& message) {
    const auto sent = channel_.send(control_protocol::encode_engine_to_ui(message));
    if (sent) {
        ui_connected_ = true;
        return;
    }
    switch (sent.error()) {
        case control_protocol::ChannelError::kClosed:
            ui_connected_ = false;
            break;
        case control_protocol::ChannelError::kTooLarge:
            // スナップショットの大きさは session の上限で抑えているので、ここに来るのは上限の
            // 見積もりの誤り。UI の表示が古いままになるので、原因を追えるように残す。
            logger_.log(common::LogLevel::kError,
                        "UI へのメッセージが上限の大きさを超えたため送れませんでした");
            break;
    }
}

}  // namespace vocroz::engine
