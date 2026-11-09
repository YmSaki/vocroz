// SPDX-License-Identifier: AGPL-3.0-only
//
// Win32 のメッセージボックスによる確認のダイアログと、OS の表示言語の取得。
// モーダルダイアログは、稼働中に閉じるときの確認と、エンジンにつながらず画面が使えないときだけに
// 使う（AGENTS.md 1.1 の非モーダル UI）。メッセージボックスは自分のメッセージループを回すので、
// 表示中も JUCE のタイマーと UI のメッセージは処理され続ける。
#pragma once

#include "dialog_text.h"

namespace vocroz::pa_desktop::win {

/// 「OK」と「キャンセル」で確かめる。既定のボタン（Enter）と Esc はどちらも「キャンセル」。
/// @param text 文言
/// @return 「OK」なら true
/// スレッド安全性: メッセージスレッドから呼ぶ。閉じるまで戻らない。
[[nodiscard]] bool confirm(const ConfirmText& text);

/// 「再試行」と「キャンセル」で尋ねる。既定のボタンは「再試行」。
/// @param text 文言
/// @return 「再試行」なら true
/// スレッド安全性: メッセージスレッドから呼ぶ。閉じるまで戻らない。
[[nodiscard]] bool ask_retry(const ConfirmText& text);

/// 「OK」だけで知らせる（起動できなかったとき）。
/// @param text 文言
/// スレッド安全性: どのスレッドからも呼び出し可能。閉じるまで戻らない。
void show_error(const ConfirmText& text);

/// OS の表示言語に合わせたダイアログの言語。日本語なら日本語、それ以外は英語。
/// UI（WebView）も OS の表示言語で言語を選ぶので、画面とダイアログの言語がそろう。
[[nodiscard]] DialogLanguage os_dialog_language() noexcept;

}  // namespace vocroz::pa_desktop::win
