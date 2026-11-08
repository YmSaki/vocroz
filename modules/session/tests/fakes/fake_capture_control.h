// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト用の CaptureControl。キャプチャと送信に反映された最新の値を保持する。
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "vocroz/session/ports/capture_control.h"

namespace vocroz::session {

/// キャプチャと送信の状態を記録する CaptureControl。
/// 初期状態は「送信している」とし、session が明示的に送信ミュートしたことを確かめる。
class FakeCaptureControl final : public CaptureControl {
public:
    void select_input(const std::optional<std::string>& device_id) override {
        input_ = device_id;
    }

    void set_send_muted(bool muted, std::uint32_t epoch) override {
        send_muted_ = muted;
        epoch_ = epoch;
    }

    void set_send_level(double level_db) override {
        level_db_ = level_db;
    }

    [[nodiscard]] const std::optional<std::string>& input() const noexcept {
        return input_;
    }

    [[nodiscard]] bool send_muted() const noexcept {
        return send_muted_;
    }

    [[nodiscard]] std::uint32_t epoch() const noexcept {
        return epoch_;
    }

    [[nodiscard]] double level_db() const noexcept {
        return level_db_;
    }

private:
    std::optional<std::string> input_;
    bool send_muted_ = false;
    std::uint32_t epoch_ = 0;
    double level_db_ = 0.0;
};

}  // namespace vocroz::session
