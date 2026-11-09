// SPDX-License-Identifier: AGPL-3.0-only
//
// 未処理の例外でミニダンプを書くクラッシュハンドラー。
// ダンプは <data-dir>\crash\ に書き、最新の 5 件だけを残す。自動送信はしない
// （docs/adr/0008-runtime-behavior.md）。ダンプには音声バッファや鍵が含まれうるので、
// 内容は最小限（MiniDumpWithDataSegs）にとどめる。
#pragma once

#include <filesystem>

#include "vocroz/common/ports/logger.h"

namespace vocroz::engine::win {

/// クラッシュハンドラーを登録する。プロセスに 1 回だけ、ほかのスレッドを始める前に呼ぶ。
/// 登録の前に、古いダンプを消して kCrashDumpsKeptAtStartup 件にする（次のクラッシュで 5
/// 件になる）。
/// @param crash_dir ダンプの置き場所（<data-dir>\crash）。なければ作る
/// @param logger    フォルダを作れなかったときなどの記録先
/// スレッド安全性: メインスレッドから起動時に 1 回だけ呼ぶ。
void install_minidump_crash_handler(const std::filesystem::path& crash_dir, common::Logger& logger);

}  // namespace vocroz::engine::win
