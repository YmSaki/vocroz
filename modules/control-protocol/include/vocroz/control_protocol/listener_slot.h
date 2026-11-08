// SPDX-License-Identifier: AGPL-3.0-only
//
// MessageChannel の実装が、設定されたリスナーを保持して呼び出すための部品。
// MessageChannel::set_listener() は「戻った後は以前のリスナーの呼び出しが実行中でない」ことを
// 約束するので、その約束の守り方を経路の実装（ipc、webui）ごとに書かずに済むよう、ここに置く。
#pragma once

#include <mutex>
#include <string_view>

#include "vocroz/control_protocol/ports/message_listener.h"

namespace vocroz::control_protocol {

/// 設定されたリスナーと、その呼び出しの排他。
/// リスナーの呼び出し中は set() が待つので、set() から戻った後は以前のリスナーが呼ばれていない。
/// スレッド安全性: すべてのメンバー関数をどのスレッドからも同時に呼び出せる。
/// ただし、リスナーの中から set() を呼ぶとデッドロックする（MessageChannel の約束と同じ）。
class MessageListenerSlot {
public:
    /// リスナーを設定する。nullptr で解除する。
    /// @param listener 設定するリスナー。呼び出し側が所有する
    void set(MessageListener* listener) {
        const std::lock_guard lock{mutex_};
        listener_ = listener;
    }

    /// リスナーにメッセージを渡す。リスナーがなければ捨てる。
    /// @param message 1 メッセージ分の JSON
    void deliver_message(std::string_view message) {
        const std::lock_guard lock{mutex_};
        if (listener_ != nullptr) {
            listener_->on_message(message);
        }
    }

    /// リスナーに経路が閉じたことを知らせる。リスナーがなければ何もしない。
    void deliver_closed() {
        const std::lock_guard lock{mutex_};
        if (listener_ != nullptr) {
            listener_->on_closed();
        }
    }

private:
    std::mutex mutex_;
    MessageListener* listener_ = nullptr;
};

}  // namespace vocroz::control_protocol
