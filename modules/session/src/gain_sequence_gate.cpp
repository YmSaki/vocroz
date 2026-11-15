// SPDX-License-Identifier: AGPL-3.0-only
//
// SetGain の送り元ごとの順序の判定の実装。

#include "gain_sequence_gate.h"

namespace vocroz::session {

bool GainSequenceGate::admit(std::string_view origin, control_protocol::Sequence seq) {
    if (origin_ && *origin_ == origin && seq < last_seq_) {
        return false;
    }
    origin_.emplace(origin);
    last_seq_ = seq;
    return true;
}

void GainSequenceGate::reset() {
    origin_.reset();
    last_seq_ = 0;
}

}  // namespace vocroz::session
