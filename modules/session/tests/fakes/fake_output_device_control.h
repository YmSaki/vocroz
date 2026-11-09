// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト用の OutputDeviceControl。登録したデバイスだけを開け、開いているデバイスを記録する。
#pragma once

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "vocroz/session/ports/output_device_control.h"

namespace vocroz::session {

/// 登録したデバイスの一覧を返し、その中のデバイスだけを開く OutputDeviceControl。
/// 開いたまま別のデバイスを開こうとしたら、テストの失敗として記録する。
class FakeOutputDeviceControl final : public OutputDeviceControl {
public:
    std::vector<control_protocol::OutputDevice> list_outputs() override {
        return devices_;
    }

    common::Result<std::string, control_protocol::ErrorCode> open_output(
        const std::string& device_id) override {
        if (opened_) {
            opened_while_open_ = true;
        }
        const auto it = std::find_if(devices_.begin(), devices_.end(),
                                     [&](const auto& d) { return d.id == device_id; });
        if (it == devices_.end() || failing_) {
            return common::make_unexpected(control_protocol::ErrorCode::kDeviceUnavailable);
        }
        opened_ = device_id;
        return it->name;
    }

    void close_output() override {
        opened_.reset();
    }

    /// 使えるデバイスを追加する。
    void add(std::string id, std::string name) {
        devices_.push_back(control_protocol::OutputDevice{std::move(id), std::move(name)});
    }

    /// デバイスを一覧から消す（抜去の再現）。開いていても閉じない。
    void remove(const std::string& id) {
        std::erase_if(devices_, [&](const auto& d) { return d.id == id; });
    }

    /// 以後の open_output() を、デバイスがあっても失敗させる。
    void fail_opens() noexcept {
        failing_ = true;
    }

    [[nodiscard]] const std::optional<std::string>& opened() const noexcept {
        return opened_;
    }

    /// 開いたまま別のデバイスを開こうとしたことがあれば true。
    [[nodiscard]] bool opened_while_open() const noexcept {
        return opened_while_open_;
    }

private:
    std::vector<control_protocol::OutputDevice> devices_;
    std::optional<std::string> opened_;
    bool failing_ = false;
    bool opened_while_open_ = false;
};

}  // namespace vocroz::session
