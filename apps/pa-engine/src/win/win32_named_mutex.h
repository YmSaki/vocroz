// SPDX-License-Identifier: AGPL-3.0-only
//
// 名前付きミューテックスの RAII ラッパー。
// pa-engine は 2 つの用途で使う。1 つはデータフォルダごとの単一起動の印、もう 1 つは
// セッションの稼働中に pa-updater
// へ「本番中」を知らせる印（docs/adr/0013-installer-and-updater.md）。
// どちらもミューテックスを所有して待ち合わせるのではなく、オブジェクトの有無だけを印に使う。
// プロセスが異常終了しても OS がハンドルを閉じるので、印が残り続けることはない。
#pragma once

#include <string>

#include "vocroz/common/result.h"

namespace vocroz::engine::win {

/// 名前付きミューテックスの作成の失敗。
enum class NamedMutexError {
    kAlreadyExists,  ///< 同じ名前のオブジェクトがすでにある
    kSystemError,    ///< そのほかの OS のエラー
};

/// 名前付きミューテックスのハンドル。破棄でハンドルを閉じる。
/// スレッド安全性: 1 つのスレッドから使う。
class Win32NamedMutex {
public:
    /// 名前付きミューテックスを作る。
    /// @param name           名前（"Local\..." など）
    /// @param exclusive      true なら、同じ名前がすでにあるときに kAlreadyExists で失敗する
    /// @return ハンドル。作れなければ原因
    [[nodiscard]] static common::Result<Win32NamedMutex, NamedMutexError> create(
        const std::wstring& name, bool exclusive);

    Win32NamedMutex(const Win32NamedMutex&) = delete;
    Win32NamedMutex& operator=(const Win32NamedMutex&) = delete;
    Win32NamedMutex(Win32NamedMutex&& other) noexcept;
    Win32NamedMutex& operator=(Win32NamedMutex&& other) noexcept;
    ~Win32NamedMutex();

private:
    explicit Win32NamedMutex(void* handle) noexcept : handle_(handle) {}
    void close() noexcept;

    // windows.h を公開しないよう HANDLE を void* で持つ。
    void* handle_ = nullptr;
};

}  // namespace vocroz::engine::win
