// SPDX-License-Identifier: AGPL-3.0-only
//
// スキャンしない PluginScanControl。
// JUCE を使わない pa-engine のビルドには、スキャンのスレッドもスキャン用ヘルパーもないので、
// スキャンし直しの依頼は何もしない（docs/adr/0042-plugin-insert.md）。
#pragma once

#include "vocroz/session/ports/plugin_scan_control.h"

namespace vocroz::engine {

/// 何もしない PluginScanControl。
/// スレッド安全性: コントロールスレッドから呼ぶ。状態を持たない。
class NoPluginScanControl final : public session::PluginScanControl {
public:
    void request_rescan() override {}
};

}  // namespace vocroz::engine
