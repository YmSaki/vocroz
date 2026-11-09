// SPDX-License-Identifier: AGPL-3.0-only
//
// エンジンから UI へのメッセージを中継の途中でのぞき、セッションが稼働中（ACTIVE）かを覚える
// MessageChannel。親機モードで閉じるときの確認に使う（稼働中だけモーダルで確かめる）。
// 状態の正本はエンジンにあり、ここはエンジンが送ったスナップショットとイベントを読むだけで、
// 状態を導き出さない。メッセージの中身は変えずにそのまま渡す。
#pragma once

#include <atomic>

#include "vocroz/control_protocol/ports/message_channel.h"
#include "vocroz/control_protocol/ports/message_listener.h"

namespace vocroz::pa_desktop {

/// エンジン側の経路を包み、セッションの状態をのぞく MessageChannel。
/// スレッド安全性: send() と set_listener() は包んだ経路と同じ。session_active()
/// はどのスレッドからも 呼び出し可能。
class SessionActivityTap final : public control_protocol::MessageChannel {
public:
    /// @param inner 包むエンジン側の経路。このオブジェクトより長く生存させる
    explicit SessionActivityTap(control_protocol::MessageChannel& inner) noexcept;
    ~SessionActivityTap() override;

    SessionActivityTap(const SessionActivityTap&) = delete;
    SessionActivityTap& operator=(const SessionActivityTap&) = delete;

    [[nodiscard]] common::Result<void, control_protocol::ChannelError> send(
        std::string_view message) override;
    void set_listener(control_protocol::MessageListener* listener) override;

    /// 最後に届いたセッションの状態が ACTIVE（参加者がいる）なら true。経路が閉じたら false。
    [[nodiscard]] bool session_active() const noexcept {
        return active_.load(std::memory_order_acquire);
    }

private:
    /// 包んだ経路の受信を受け、状態を読んでから外のリスナーへ渡す。
    class Observer final : public control_protocol::MessageListener {
    public:
        explicit Observer(SessionActivityTap& owner) noexcept : owner_(owner) {}
        void on_message(std::string_view message) override;
        void on_closed() override;

    private:
        SessionActivityTap& owner_;
    };

    void observe(std::string_view message) noexcept;

    control_protocol::MessageChannel& inner_;
    Observer observer_;
    control_protocol::MessageListener* listener_ = nullptr;
    std::atomic<bool> active_{false};
};

}  // namespace vocroz::pa_desktop
