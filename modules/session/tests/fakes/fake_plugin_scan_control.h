// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト用の PluginScanControl。依頼の回数を数えるだけで、結果はテストが
// HostSession::set_plugin_scanning() と set_available_plugins() で届ける。
#pragma once

#include "vocroz/session/ports/plugin_scan_control.h"

namespace vocroz::session {

/// 依頼を数える PluginScanControl。
class FakePluginScanControl final : public PluginScanControl {
public:
    void request_rescan() override {
        ++rescans_;
    }

    /// @return request_rescan() を呼んだ回数
    [[nodiscard]] int rescans() const noexcept {
        return rescans_;
    }

private:
    int rescans_ = 0;
};

}  // namespace vocroz::session
