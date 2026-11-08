// SPDX-License-Identifier: AGPL-3.0-only
//
// noise-c の Curve448 と NewHope の DH を、生成に失敗する実装に差し替える。
// 両者の実装は GCC 拡張に依存して MSVC でビルドできず、VOCROZ は Noise_IK_25519 しか使わないため
// （docs/adr/0001-key-exchange-noise-ik.md）。
// NULL を返すと、noise_dhstate_new_by_id() は NOISE_ERROR_NO_MEMORY を返して失敗する。

#include "internal.h"

NoiseDHState* noise_curve448_new(void) {
    return 0;
}

NoiseDHState* noise_newhope_new(void) {
    return 0;
}
