// SPDX-License-Identifier: AGPL-3.0-only
//
// UiBridge の実装。

#include "ui_bridge.h"

#include <string>
#include <utility>

#include "vocroz/control_protocol/json_codec.h"

namespace vocroz::client {

UiBridge::UiBridge(control_protocol::MessageChannel& channel, common::Logger& logger)
    : channel_(channel), logger_(logger) {}

UiBridge::~UiBridge() {
    channel_.set_listener(nullptr);
}

void UiBridge::start(Handler handler) {
    handler_ = std::move(handler);
    channel_.set_listener(this);
}

void UiBridge::on_message(std::string_view message) {
    const auto decoded = control_protocol::decode_ui_to_engine(message);
    if (!decoded) {
        // 値は含めず、フィールド名だけを残す（AGENTS.md 6.4）。
        logger_.log(
            common::LogLevel::kWarning,
            "client: a malformed UI message was dropped (field: " + decoded.error().field + ")");
        return;
    }
    if (handler_) {
        handler_(decoded.value());
    }
}

void UiBridge::on_closed() {}

void UiBridge::publish(const control_protocol::EngineToUiMessage& message) {
    // 失敗は UI の読み込み中（捨ててよい）か、大きすぎるメッセージ（エンジンの不具合）。
    if (!channel_.send(control_protocol::encode_engine_to_ui(message))) {
        logger_.log(common::LogLevel::kDebug, "client: a message to the UI was dropped");
    }
}

}  // namespace vocroz::client
