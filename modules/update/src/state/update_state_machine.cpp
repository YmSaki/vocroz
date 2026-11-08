// SPDX-License-Identifier: AGPL-3.0-only
//
// update_state_machine.h の遷移表。

#include "state/update_state_machine.h"

#include <array>
#include <cstddef>

namespace vocroz::update::state {
namespace {

using S = UpdateState;

constexpr std::size_t kStateCount = static_cast<std::size_t>(S::kAborted) + 1;
constexpr std::size_t kEventCount = static_cast<std::size_t>(UpdateEvent::kRollbackFinished) + 1;

// 行は状態、列はイベント（kStart、kStepSucceeded、kStepFailed、kRollbackFinished の順）。
// 自分自身への遷移は「そのイベントは受け付けない」を表す。
// kExtracting 以降の失敗だけが kRollingBack に進むのは、その段階からインストール先
// （新しいフォルダ、ポインター）が変わり始め、旧版に戻す処理が要るため。
constexpr std::array<std::array<S, kEventCount>, kStateCount> kTransitions = {{
    /* kIdle               */ {S::kVerifyingPackage, S::kIdle, S::kIdle, S::kIdle},
    /* kVerifyingPackage   */
    {S::kVerifyingPackage, S::kBackingUp, S::kAborted, S::kVerifyingPackage},
    /* kBackingUp          */ {S::kBackingUp, S::kExtracting, S::kAborted, S::kBackingUp},
    /* kExtracting         */
    {S::kExtracting, S::kVerifyingSignature, S::kRollingBack, S::kExtracting},
    /* kVerifyingSignature */
    {S::kVerifyingSignature, S::kCheckingHealth, S::kRollingBack, S::kVerifyingSignature},
    /* kCheckingHealth     */
    {S::kCheckingHealth, S::kSwitching, S::kRollingBack, S::kCheckingHealth},
    /* kSwitching          */ {S::kSwitching, S::kCommitted, S::kRollingBack, S::kSwitching},
    /* kCommitted          */ {S::kCommitted, S::kCommitted, S::kCommitted, S::kCommitted},
    /* kRollingBack        */
    {S::kRollingBack, S::kRollingBack, S::kRollingBack, S::kRolledBack},
    /* kRolledBack         */ {S::kRolledBack, S::kRolledBack, S::kRolledBack, S::kRolledBack},
    /* kAborted            */ {S::kAborted, S::kAborted, S::kAborted, S::kAborted},
}};

}  // namespace

UpdateState next_state(UpdateState state, UpdateEvent event) noexcept {
    return kTransitions[static_cast<std::size_t>(state)][static_cast<std::size_t>(event)];
}

bool is_terminal(UpdateState state) noexcept {
    switch (state) {
        case S::kCommitted:
        case S::kRolledBack:
        case S::kAborted:
            return true;
        case S::kIdle:
        case S::kVerifyingPackage:
        case S::kBackingUp:
        case S::kExtracting:
        case S::kVerifyingSignature:
        case S::kCheckingHealth:
        case S::kSwitching:
        case S::kRollingBack:
            return false;
    }
    return false;
}

}  // namespace vocroz::update::state
