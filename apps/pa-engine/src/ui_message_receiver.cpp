// SPDX-License-Identifier: AGPL-3.0-only
//
// ui_message_receiver.h の実装。

#include "ui_message_receiver.h"

#include <string>
#include <utility>

#include "vocroz/control_protocol/json_codec.h"

namespace vocroz::engine {

void UiMessageReceiver::on_message(std::string_view message) {
    auto decoded = control_protocol::decode_ui_to_engine(message);
    if (!decoded) {
        const auto& error = decoded.error();
        logger_.log(common::LogLevel::kWarning,
                    "UI からの不正なメッセージを捨てました（フィールド: " +
                        (error.field.empty() ? std::string{"なし"} : error.field) + "）");
        return;
    }
    loop_.post([this, command = std::move(decoded).value()] { handler_(command); });
}

void UiMessageReceiver::on_closed() {
    logger_.log(common::LogLevel::kError,
                "UI の待ち受けを続けられなくなりました。音声とセッションは続けます");
}

}  // namespace vocroz::engine
