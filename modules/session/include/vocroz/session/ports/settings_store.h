// SPDX-License-Identifier: AGPL-3.0-only
//
// 設定の読み込みと保存のポート。persistence がアトミックな保存とバックアップで実装する。
// 設定値の意味づけ（既定値への戻し方など）は session が持ち、ポートは値を運ぶだけにする。
#pragma once

#include <span>
#include <vector>

#include "vocroz/common/result.h"
#include "vocroz/session/settings.h"

namespace vocroz::session {

/// 親機と子機の設定の保存先。
/// 親機の挿入のプラグインの状態は、設定とは別に読み書きする。大きさが設定の数百倍になることがあり、
/// 出力デバイスを替えたときなどの設定の保存のたびに書き直さないため（docs/adr/0042-plugin-insert.md）。
/// スレッド安全性: コントロールスレッドからだけ呼ぶ。実装はファイル I/O でブロックしてよい。
class SettingsStore {
public:
    virtual ~SettingsStore() = default;

    /// 親機の設定を読む。
    /// @return 設定。読めなければ理由。呼び出し側は既定値で続行する
    [[nodiscard]] virtual common::Result<HostSettings, SettingsError> load_host() = 0;

    /// 親機の設定を保存する。途中で強制終了しても、前回の設定か今回の設定のどちらかが残る。
    /// @param settings 保存する設定
    /// @return 失敗したら理由。呼び出し側はログに残して続行する
    [[nodiscard]] virtual common::Result<void, SettingsError> save_host(
        const HostSettings& settings) = 0;

    /// 親機の挿入のプラグインの状態を読む。
    /// @return 状態。読めなければ理由。呼び出し側はプラグインの既定の状態で続行する
    [[nodiscard]] virtual common::Result<std::vector<SavedPluginState>, SettingsError>
    load_plugin_states() = 0;

    /// 親機の挿入のプラグインの状態を、すべて置き換えて保存する。保存の保証は save_host() と同じ。
    /// @param states 保存する状態
    /// @return 失敗したら理由。呼び出し側はログに残して続行する
    [[nodiscard]] virtual common::Result<void, SettingsError> save_plugin_states(
        std::span<const SavedPluginState> states) = 0;

    /// 子機の設定を読む。
    /// @return 設定。読めなければ理由。呼び出し側は既定値で続行する
    [[nodiscard]] virtual common::Result<ClientSettings, SettingsError> load_client() = 0;

    /// 子機の設定を保存する。保存の保証は save_host() と同じ。
    /// @param settings 保存する設定
    /// @return 失敗したら理由。呼び出し側はログに残して続行する
    [[nodiscard]] virtual common::Result<void, SettingsError> save_client(
        const ClientSettings& settings) = 0;
};

}  // namespace vocroz::session
