// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト用の ExternalOutputControl。呼ばれた送り方を記録し、開始と接続テストの失敗を再現できる。
// 鍵は記録しない（テストでも秘密情報を持ち回らない）。鍵が渡されたことだけを数える。
#pragma once

#include <optional>
#include <vector>

#include "vocroz/session/ports/external_output_control.h"

namespace vocroz::session {

/// 呼ばれた操作を記録する ExternalOutputControl。
class FakeExternalOutputControl final : public ExternalOutputControl {
public:
    common::Result<void, control_protocol::ExternalOutputResult> start(
        const ExternalOutputPlan& plan, const transport::SrtpMasterKey& /*key*/) override {
        starts_.push_back(plan);
        if (start_error_) {
            return common::make_unexpected(*start_error_);
        }
        running_ = true;
        return {};
    }

    void stop() override {
        ++stops_;
        running_ = false;
        testing_ = false;
    }

    common::Result<void, control_protocol::ExternalOutputResult> start_test(
        const ExternalOutputPlan& plan, const transport::SrtpMasterKey& /*key*/) override {
        tests_.push_back(plan);
        if (start_error_) {
            return common::make_unexpected(*start_error_);
        }
        testing_ = true;
        return {};
    }

    /// 以降の start() と start_test() を error で失敗させる。std::nullopt で戻す。
    void fail_start(std::optional<control_protocol::ExternalOutputResult> error) {
        start_error_ = error;
    }
    /// start() に渡された送り方（呼ばれた順）。
    [[nodiscard]] const std::vector<ExternalOutputPlan>& starts() const noexcept {
        return starts_;
    }
    /// start_test() に渡された送り方（呼ばれた順）。
    [[nodiscard]] const std::vector<ExternalOutputPlan>& tests() const noexcept {
        return tests_;
    }
    /// stop() が呼ばれた回数。
    [[nodiscard]] int stops() const noexcept {
        return stops_;
    }
    /// 送信中なら true。
    [[nodiscard]] bool running() const noexcept {
        return running_;
    }
    /// 接続テスト中なら true。
    [[nodiscard]] bool testing() const noexcept {
        return testing_;
    }

private:
    std::vector<ExternalOutputPlan> starts_;
    std::vector<ExternalOutputPlan> tests_;
    std::optional<control_protocol::ExternalOutputResult> start_error_;
    int stops_ = 0;
    bool running_ = false;
    bool testing_ = false;
};

}  // namespace vocroz::session
