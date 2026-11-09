// SPDX-License-Identifier: AGPL-3.0-only
//
// win32_dialogs.h の実装。

#include "win/win32_dialogs.h"

#include <string>
#include <string_view>

#include <windows.h>

namespace vocroz::pa_desktop::win {
namespace {

[[nodiscard]] std::wstring widen(std::string_view utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int length =
        ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(),
                          length);
    return wide;
}

[[nodiscard]] int show(const ConfirmText& text, UINT type) {
    const std::wstring title = widen(text.title);
    const std::wstring message = widen(text.message);
    // 前面のウィンドウを親にして、ダイアログが画面の後ろに隠れないようにする。
    return ::MessageBoxW(::GetActiveWindow(), message.c_str(), title.c_str(),
                         type | MB_SETFOREGROUND);
}

}  // namespace

bool confirm(const ConfirmText& text) {
    // 誤って閉じないよう、既定のボタンは 2 つ目（キャンセル）にする。
    return show(text, MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) == IDOK;
}

bool ask_retry(const ConfirmText& text) {
    return show(text, MB_RETRYCANCEL | MB_ICONERROR) == IDRETRY;
}

void show_error(const ConfirmText& text) {
    static_cast<void>(show(text, MB_OK | MB_ICONERROR));
}

DialogLanguage os_dialog_language() noexcept {
    return PRIMARYLANGID(::GetUserDefaultUILanguage()) == LANG_JAPANESE ? DialogLanguage::kJapanese
                                                                        : DialogLanguage::kEnglish;
}

}  // namespace vocroz::pa_desktop::win
