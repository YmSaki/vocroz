// SPDX-License-Identifier: AGPL-3.0-only
//
// remote-pa がネイティブのダイアログで出す文言（日本語と英語）。
// 画面の文字列は ui/ の文字列リソースにあるが、ここに置く文言は WebView の外（ウィンドウを閉じる
// ときの確認と、エンジンにつながらないとき）に出すので、UI のリソースを使えない。
// 言語は --lang か OS の表示言語で決め、画面と同じ言語にそろえる。
#pragma once

#include <string_view>

namespace vocroz::pa_desktop {

/// ダイアログの言語。
enum class DialogLanguage {
    kJapanese,
    kEnglish,
};

/// 確認のダイアログの文言。ボタンは OS の「OK」と「キャンセル」を使う。
struct ConfirmText {
    std::string_view title;
    std::string_view message;
};

/// 親機モードで、稼働中のセッションがあるのに閉じようとしたときの確認。
/// エンジンは閉じた後も動き続けるので、音が止まらないことと、開き直せば戻れることを伝える。
[[nodiscard]] ConfirmText host_close_text(DialogLanguage language) noexcept;

/// 子機モードで、参加中に閉じようとしたときの確認。閉じると親機から切断される。
[[nodiscard]] ConfirmText client_close_text(DialogLanguage language) noexcept;

/// エンジンにつなげなかったときの文言。「再試行」でもう一度つなぎ、「キャンセル」で閉じる。
[[nodiscard]] ConfirmText engine_unavailable_text(DialogLanguage language) noexcept;

/// 起動できなかったときの文言。原因はログに書くので、ログの場所を案内する。
[[nodiscard]] ConfirmText startup_failed_text(DialogLanguage language) noexcept;

/// --lang の値（ja、en）から言語を決める。
/// @param lang --lang の値
/// @return ja なら日本語、それ以外は英語
[[nodiscard]] DialogLanguage dialog_language_from_tag(std::string_view lang) noexcept;

}  // namespace vocroz::pa_desktop
