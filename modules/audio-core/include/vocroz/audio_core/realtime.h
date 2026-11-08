// SPDX-License-Identifier: AGPL-3.0-only
//
// オーディオスレッドで実行する関数に付ける注釈。
// Clang の RealtimeSanitizer（-fsanitize=realtime）を有効にしてビルドすると、
// この注釈を付けた関数の実行中にヒープ確保、ロック、システムコールなどの
// 実行時間に上限がない操作（AGENTS.md 第 9 節）を検出してプログラムを止める。
// RealtimeSanitizer を使わないビルドでは何も展開しない。
#pragma once

// 注釈は属性なので関数ではなくマクロでしか書けない（AGENTS.md 8.2 の例外）。
#if defined(__has_feature)
#if __has_feature(realtime_sanitizer)
#define VOCROZ_NONBLOCKING [[clang::nonblocking]]
#endif
#endif

#ifndef VOCROZ_NONBLOCKING
/// オーディオスレッドから呼ぶ関数（ブロックしない関数）に付ける。
/// 関数の型に付く属性なので、宣言と定義の両方で noexcept の後（override や = 0 の前）に書く。
/// 例: `void render(InterleavedBuffer out) noexcept VOCROZ_NONBLOCKING override;`
/// RealtimeSanitizer の有効なビルドでは [[clang::nonblocking]] に展開する。
#define VOCROZ_NONBLOCKING
#endif
