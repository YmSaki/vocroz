// SPDX-License-Identifier: AGPL-3.0-only
//
// pa-engine のプロセスを起動するポート。コンポジションルートが Win32 の CreateProcessW で
// 実装し（src/win/win32_engine_launcher.h）、テストではフェイクに差し替える。
#pragma once

#include <optional>

namespace vocroz::pa_desktop {

/// エンジンのプロセスの起動。
class EngineLauncher {
public:
    virtual ~EngineLauncher() = default;

    /// エンジンを起動する。前に起動したプロセスの状態は捨てる。
    /// @return 起動できたら true（準備ができたかどうかは、パイプにつながるかで判断する）
    /// スレッド安全性: コントロールスレッドから呼ぶ。
    [[nodiscard]] virtual bool launch() = 0;

    /// 最後に起動したプロセスの終了コードを返す。ブロックしない。
    /// @return 終了していればその終了コード。動いているか、起動していなければ std::nullopt
    /// スレッド安全性: コントロールスレッドから呼ぶ。
    [[nodiscard]] virtual std::optional<int> exit_code() = 0;
};

}  // namespace vocroz::pa_desktop
