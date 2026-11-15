// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト用の RecorderControl。呼ばれた録り方を記録し、開始の失敗と、録音中の欠落と失敗を
// 再現できる。
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "vocroz/session/ports/recorder_control.h"

namespace vocroz::session {

/// 呼ばれた操作を記録する RecorderControl。
class FakeRecorderControl final : public RecorderControl {
public:
    common::Result<std::string, control_protocol::RecorderResult> start(
        const RecordingPlan& plan) override {
        starts_.push_back(plan);
        if (start_error_) {
            return common::make_unexpected(*start_error_);
        }
        recording_ = true;
        status_ = RecordingStatus{std::vector<std::uint64_t>(plan.tracks.size(), 0), std::nullopt};
        return take_;
    }

    void stop() override {
        ++stops_;
        recording_ = false;
    }

    RecordingStatus status() const override {
        return recording_ ? status_ : RecordingStatus{};
    }

    /// 次の start() を失敗させる。std::nullopt で成功に戻す。
    void fail_start(std::optional<control_protocol::RecorderResult> error) {
        start_error_ = error;
    }
    /// 次の start() が返すテイクの名前。
    void set_take(std::string take) {
        take_ = std::move(take);
    }
    /// トラックの欠落（フレーム数）を設定する。
    void set_dropped(std::size_t track, std::uint64_t frames) {
        status_.dropped_frames.at(track) = frames;
    }
    /// 録音中の書き出しの失敗を再現する。
    void fail(audio_core::AudioFileError error) {
        status_.failure = error;
    }

    [[nodiscard]] const std::vector<RecordingPlan>& starts() const noexcept {
        return starts_;
    }
    [[nodiscard]] int stops() const noexcept {
        return stops_;
    }
    [[nodiscard]] bool recording() const noexcept {
        return recording_;
    }

private:
    std::vector<RecordingPlan> starts_;
    int stops_ = 0;
    bool recording_ = false;
    std::optional<control_protocol::RecorderResult> start_error_;
    std::string take_ = "20261115-120000";
    RecordingStatus status_;
};

}  // namespace vocroz::session
