// SPDX-License-Identifier: AGPL-3.0-only
//
// オーディオスレッドを MMCSS の「Pro Audio」タスクに登録する RAII ラッパー。
// プロセスの優先度クラスやシステムのタイマー分解能は変えず、このスレッドの間だけ優先度を上げる
// （docs/adr/0008-runtime-behavior.md）。
#pragma once

#include <windows.h>

namespace vocroz::device::win {

/// 生成したスレッドを MMCSS に登録し、破棄時に登録を戻す。
/// 生成と破棄は同じスレッドで行う。
class MmcssRegistration {
public:
    MmcssRegistration() noexcept;
    ~MmcssRegistration();
    MmcssRegistration(const MmcssRegistration&) = delete;
    MmcssRegistration& operator=(const MmcssRegistration&) = delete;

    /// 登録できたら true を返す。登録できなくても音は出せるので、呼び出し側はログに残すだけにする。
    [[nodiscard]] bool is_registered() const noexcept {
        return handle_ != nullptr;
    }

private:
    HANDLE handle_ = nullptr;
};

}  // namespace vocroz::device::win
