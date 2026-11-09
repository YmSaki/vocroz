// SPDX-License-Identifier: AGPL-3.0-only
//
// 送信スレッドを起こす合図。オーディオスレッドとコントロールスレッドが、送るものを SPSC キューに
// 入れた直後に notify() を呼ぶ。送信スレッドは合図があるまで眠り、ポーリングの周期ぶんの遅延を
// 音声に足さない。
#pragma once

#include <atomic>
#include <cstdint>

#include "vocroz/audio_core/realtime.h"

namespace vocroz::client {

/// 送信スレッドを起こす合図。
class SendWakeup {
public:
    /// 送信スレッドを起こす。合図は数えるだけで、待っているスレッドがなくても失われない。
    /// スレッド安全性: どのスレッドからも同時に呼び出し可能。オーディオスレッドから呼び出し可能
    /// （アトミックな加算と、待っているスレッドを起こすだけでブロックしない）。
    void notify() noexcept VOCROZ_NONBLOCKING {
        generation_.fetch_add(1, std::memory_order_release);
        generation_.notify_one();
    }

    /// 現在の合図の世代を返す。送信スレッドは、処理を始める前にこれを読み、処理の後に wait()
    /// に渡す。処理の途中で届いた合図を取りこぼさないようにするため。
    /// スレッド安全性: どのスレッドからも呼び出し可能。
    [[nodiscard]] std::uint32_t generation() const noexcept {
        return generation_.load(std::memory_order_acquire);
    }

    /// 世代が seen から進むまで待つ。すでに進んでいればすぐに戻る。
    /// @param seen generation() で読んだ値
    /// スレッド安全性: 送信スレッドだけが呼ぶ。ブロックする。
    void wait(std::uint32_t seen) const noexcept {
        generation_.wait(seen, std::memory_order_acquire);
    }

private:
    std::atomic<std::uint32_t> generation_{0};
};

}  // namespace vocroz::client
