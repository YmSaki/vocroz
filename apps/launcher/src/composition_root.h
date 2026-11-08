// SPDX-License-Identifier: AGPL-3.0-only
//
// 起動スタブのコンポジションルート。ポインターファイルを読み、update の launch_candidates() が
// 示す順にバージョン別フォルダの remote-pa を起動する。
#pragma once

#include <string>

namespace vocroz::launcher {

class CompositionRoot {
public:
    /// @param command_line GetCommandLineW() の値
    explicit CompositionRoot(std::wstring command_line);

    /// remote-pa を起動する。起動したら待たずに戻る。
    /// @return 起動できたら 0。どの版も起動できなければ 1（利用者にダイアログで知らせる）。
    [[nodiscard]] int run();

private:
    std::wstring command_line_;
};

}  // namespace vocroz::launcher
