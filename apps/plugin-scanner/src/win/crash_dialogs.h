// SPDX-License-Identifier: AGPL-3.0-only
//
// Windows のエラー報告のダイアログを出さないようにする。
#pragma once

namespace vocroz::plugin_scanner::win {

/// このプロセスが落ちたときと、ドライブのエラーのときに、ダイアログを出さないようにする。
/// 出すとヘルパーはダイアログが閉じられるまで終わらず、pa-engine は落ちたプラグインを
/// 制限時間まで待つことになるため。スレッド安全性: main() の最初で 1 回呼ぶ。
void suppress_crash_dialogs() noexcept;

}  // namespace vocroz::plugin_scanner::win
