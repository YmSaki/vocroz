// SPDX-License-Identifier: AGPL-3.0-only
//
// device_loss_relay.h の実装。

#include "device_loss_relay.h"

namespace vocroz::engine {

void DeviceLossRelay::on_device_lost() noexcept {
    // 別のデバイスへは切り替えず、まず出力を止める。session が Master を SAFETY でミュートする
    // より前に、次のコールバックから無音にする。
    mixer_.engage_safety_mute();
    try {
        loop_.post([this] { on_lost_(); });
    } catch (...) {
        // 積めなくても安全ミュートはかかっているので、音は出ない。session の表示だけが遅れる。
    }
}

}  // namespace vocroz::engine
