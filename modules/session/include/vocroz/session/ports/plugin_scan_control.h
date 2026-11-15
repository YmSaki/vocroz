// SPDX-License-Identifier: AGPL-3.0-only
//
// プラグインのスキャンし直しを依頼するポート。
// スキャンはファイルごとにヘルパーを起動して時間がかかるので、pa-engine のスキャン専用のスレッドで
// 行う。session は依頼するだけで、スキャン中かどうかと結果は HostSession::set_plugin_scanning() と
// HostSession::set_available_plugins() で受け取る（docs/adr/0042-plugin-insert.md）。
#pragma once

namespace vocroz::session {

/// プラグインのスキャンし直しの依頼。
/// スレッド安全性: コントロールスレッドから呼ぶ。実装はブロックせず、依頼を別のスレッドへ渡す。
class PluginScanControl {
public:
    virtual ~PluginScanControl() = default;

    /// スキャンし直す。スキャン中なら、今のスキャンが済んでからもう一度スキャンする。
    virtual void request_rescan() = 0;
};

}  // namespace vocroz::session
