// SPDX-License-Identifier: AGPL-3.0-only
//
// dialog_text.h の実装。

#include "dialog_text.h"

namespace vocroz::pa_desktop {

ConfirmText host_close_text(DialogLanguage language) noexcept {
    switch (language) {
        case DialogLanguage::kJapanese:
            return {.title = "VOCROZ",
                    .message =
                        "参加者がいるセッションが進行中です。\n"
                        "画面を閉じても、音声の処理と参加者との接続は続きます。"
                        "もう一度開くと、この画面に戻れます。\n\n画面を閉じますか？"};
        case DialogLanguage::kEnglish:
            return {.title = "VOCROZ",
                    .message =
                        "A session with participants is in progress.\n"
                        "Closing this window keeps the audio and the participants "
                        "connected. Open VOCROZ again to return to it.\n\n"
                        "Close the window?"};
    }
    return host_close_text(DialogLanguage::kEnglish);
}

ConfirmText client_close_text(DialogLanguage language) noexcept {
    switch (language) {
        case DialogLanguage::kJapanese:
            return {.title = "VOCROZ",
                    .message =
                        "セッションに参加中です。\n閉じると親機との接続が切れ、"
                        "音声の送信が止まります。\n\n閉じますか？"};
        case DialogLanguage::kEnglish:
            return {.title = "VOCROZ",
                    .message =
                        "You are in a session.\nClosing disconnects you from the host "
                        "and stops sending audio.\n\nClose VOCROZ?"};
    }
    return client_close_text(DialogLanguage::kEnglish);
}

ConfirmText engine_unavailable_text(DialogLanguage language) noexcept {
    switch (language) {
        case DialogLanguage::kJapanese:
            return {.title = "VOCROZ",
                    .message =
                        "音声エンジンを起動できないか、接続できませんでした。\n"
                        "「再試行」でもう一度試します。「キャンセル」で閉じます。"};
        case DialogLanguage::kEnglish:
            return {.title = "VOCROZ",
                    .message =
                        "The audio engine could not be started or reached.\n"
                        "Choose Retry to try again, or Cancel to close."};
    }
    return engine_unavailable_text(DialogLanguage::kEnglish);
}

ConfirmText startup_failed_text(DialogLanguage language) noexcept {
    switch (language) {
        case DialogLanguage::kJapanese:
            return {.title = "VOCROZ",
                    .message =
                        "VOCROZ を起動できませんでした。\n"
                        "原因はデータフォルダの logs フォルダのログに記録しています。"};
        case DialogLanguage::kEnglish:
            return {.title = "VOCROZ",
                    .message =
                        "VOCROZ could not start.\n"
                        "The cause is recorded in the log in the logs folder of the data "
                        "folder."};
    }
    return startup_failed_text(DialogLanguage::kEnglish);
}

std::string_view routing_window_title(DialogLanguage language) noexcept {
    switch (language) {
        case DialogLanguage::kJapanese:
            return "VOCROZ - ルーティング";
        case DialogLanguage::kEnglish:
            return "VOCROZ - Routing";
    }
    return routing_window_title(DialogLanguage::kEnglish);
}

DialogLanguage dialog_language_from_tag(std::string_view lang) noexcept {
    return lang == "ja" ? DialogLanguage::kJapanese : DialogLanguage::kEnglish;
}

}  // namespace vocroz::pa_desktop
