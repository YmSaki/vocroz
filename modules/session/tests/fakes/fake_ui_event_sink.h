// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト用の UiEventSink。送られたメッセージを順に記録し、テストが種類ごとに取り出せるようにする。
#pragma once

#include <optional>
#include <vector>

#include "vocroz/session/ports/ui_event_sink.h"

namespace vocroz::session {

/// 送られたメッセージを記録する UiEventSink。
class FakeUiEventSink final : public UiEventSink {
public:
    void publish(const control_protocol::EngineToUiMessage& message) override {
        messages_.push_back(message);
    }

    /// 送られたメッセージ（送られた順）。
    [[nodiscard]] const std::vector<control_protocol::EngineToUiMessage>& messages()
        const noexcept {
        return messages_;
    }

    /// 型 T のメッセージを、送られた順にすべて返す。
    template <typename T>
    [[nodiscard]] std::vector<T> all() const {
        std::vector<T> out;
        for (const auto& m : messages_) {
            if (const auto* t = std::get_if<T>(&m)) {
                out.push_back(*t);
            }
        }
        return out;
    }

    /// 型 T の最後のメッセージを返す。なければ std::nullopt。
    template <typename T>
    [[nodiscard]] std::optional<T> last() const {
        for (auto it = messages_.rbegin(); it != messages_.rend(); ++it) {
            if (const auto* t = std::get_if<T>(&*it)) {
                return *t;
            }
        }
        return std::nullopt;
    }

    /// 記録を消す。
    void clear() noexcept {
        messages_.clear();
    }

private:
    std::vector<control_protocol::EngineToUiMessage> messages_;
};

}  // namespace vocroz::session
