// SPDX-License-Identifier: AGPL-3.0-only
//
// 起動スタブ vocroz.exe のエントリポイント。引数のパースはせず、コンポジションルートに
// コマンドラインを渡して実行するだけにする（docs/adr/0009-installer-and-updater.md）。
// コンソールを出さないよう、Windows のサブシステムで動く。

#include <windows.h>

#include "composition_root.h"

int WINAPI wWinMain(HINSTANCE /*instance*/, HINSTANCE /*previous*/, PWSTR /*command_line*/,
                    int /*show*/) {
    vocroz::launcher::CompositionRoot root{::GetCommandLineW()};
    return root.run();
}
