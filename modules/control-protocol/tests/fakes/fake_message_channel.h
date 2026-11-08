// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト用の MessageChannel。送ったメッセージを記録し、受信と切断をテストから起こせるようにする。
#pragma once

#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "vocroz/control_protocol/constants.h"
#include "vocroz/control_protocol/ports/message_channel.h"

namespace vocroz::control_protocol {

/// メモリ上の MessageChannel。
/// スレッド安全性: send() と sent() はどのスレッドからも同時に呼び出せる。
/// deliver() と close() はテストのスレッドから呼び、リスナーはそのスレッドで同期的に呼ばれる。
class FakeMessageChannel final : public MessageChannel {
public:
    common::Result<void, ChannelError> send(std::string_view message) override {
        const std::lock_guard lock{mutex_};
        if (closed_) {
            return common::make_unexpected(ChannelError::kClosed);
        }
        if (message.size() > kMaxMessageBytes) {
            return common::make_unexpected(ChannelError::kTooLarge);
        }
        sent_.emplace_back(message);
        return {};
    }

    void set_listener(MessageListener* listener) override {
        const std::lock_guard lock{mutex_};
        listener_ = listener;
    }

    /// 相手からメッセージが届いたことにする。リスナーがなければ捨てる。
    void deliver(std::string_view message) {
        if (MessageListener* listener = current_listener()) {
            listener->on_message(message);
        }
    }

    /// 経路が閉じたことにする。以降の send() は kClosed で失敗する。
    void close() {
        {
            const std::lock_guard lock{mutex_};
            closed_ = true;
        }
        if (MessageListener* listener = current_listener()) {
            listener->on_closed();
        }
    }

    /// これまでに送られたメッセージを、送られた順に返す。
    [[nodiscard]] std::vector<std::string> sent() const {
        const std::lock_guard lock{mutex_};
        return sent_;
    }

    /// 設定されているリスナー。
    [[nodiscard]] MessageListener* current_listener() const {
        const std::lock_guard lock{mutex_};
        return listener_;
    }

private:
    mutable std::mutex mutex_;
    MessageListener* listener_ = nullptr;
    bool closed_ = false;
    std::vector<std::string> sent_;
};

}  // namespace vocroz::control_protocol
