// SPDX-License-Identifier: AGPL-3.0-only
//
// session_activity_tap.h の実装。

#include "session_activity_tap.h"

#include <type_traits>
#include <variant>

#include "vocroz/control_protocol/json_codec.h"

namespace vocroz::pa_desktop {

SessionActivityTap::SessionActivityTap(control_protocol::MessageChannel& inner) noexcept
    : inner_(inner), observer_(*this) {}

SessionActivityTap::~SessionActivityTap() {
    inner_.set_listener(nullptr);
}

common::Result<void, control_protocol::ChannelError> SessionActivityTap::send(
    std::string_view message) {
    return inner_.send(message);
}

void SessionActivityTap::set_listener(control_protocol::MessageListener* listener) {
    // 先に受信を止めてから入れ替える。包んだ経路の set_listener() は、戻った後に以前の
    // リスナー（observer_）の呼び出しが実行中でないことを保証するので、listener_
    // を安全に変えられる。
    inner_.set_listener(nullptr);
    listener_ = listener;
    if (listener_ != nullptr) {
        inner_.set_listener(&observer_);
    }
}

void SessionActivityTap::observe(std::string_view message) noexcept {
    // メーターも含めてすべてデコードする。種類名で先に振り分けると control-protocol の種類名を
    // ここにも持つことになるうえ、UI のプロセスでのデコードの負荷は音声に影響しない。
    try {
        const auto decoded = control_protocol::decode_engine_to_ui(message);
        if (!decoded) {
            return;
        }
        std::visit(
            [this](const auto& event) {
                using Event = std::decay_t<decltype(event)>;
                if constexpr (std::is_same_v<Event, control_protocol::MixerSnapshotEvent>) {
                    active_.store(
                        event.snapshot.session.state == control_protocol::SessionState::kActive,
                        std::memory_order_release);
                } else if constexpr (std::is_same_v<Event, control_protocol::SessionUpdated>) {
                    active_.store(event.session.state == control_protocol::SessionState::kActive,
                                  std::memory_order_release);
                }
            },
            decoded.value());
    } catch (...) {
        // デコードの失敗は中継に影響させない（のぞくだけなので、状態はそのまま残す）。
    }
}

void SessionActivityTap::Observer::on_message(std::string_view message) {
    owner_.observe(message);
    if (owner_.listener_ != nullptr) {
        owner_.listener_->on_message(message);
    }
}

void SessionActivityTap::Observer::on_closed() {
    // エンジンとつながっていなければ、閉じても止まるものは画面にない。
    owner_.active_.store(false, std::memory_order_release);
    if (owner_.listener_ != nullptr) {
        owner_.listener_->on_closed();
    }
}

}  // namespace vocroz::pa_desktop
