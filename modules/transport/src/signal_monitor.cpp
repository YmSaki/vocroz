// SPDX-License-Identifier: AGPL-3.0-only
//
// signal_monitor.h の実装。世代番号の比較は SendMuteTracker、遷移規則は遷移表に任せ、
// ここでは両者をつなぎ、タイムアウトの起点の時刻を管理する。

#include "vocroz/transport/signal_monitor.h"

#include "state/signal_state_machine.h"
#include "transport_constants.h"

namespace vocroz::transport {

SignalMonitor::SignalMonitor(TimePoint now) noexcept : last_frame_(now), resume_started_(now) {}

bool SignalMonitor::on_media_frame(std::uint32_t mute_epoch, TimePoint now) noexcept {
    switch (mute_.classify(mute_epoch)) {
        case FrameVerdict::kAccept:
            break;
        case FrameVerdict::kAcceptNewSegment:
            // 解除の通知より先に新しい世代のフレームが届いた。解除を知ったものとして扱う。
            state_ = next_signal_state(state_, SignalEvent::kSendUnmuted);
            resume_started_ = now;
            break;
        case FrameVerdict::kDrop:
            return false;
    }
    state_ = next_signal_state(state_, SignalEvent::kFrameAccepted);
    last_frame_ = now;
    return true;
}

void SignalMonitor::on_send_mute(bool send_muted, std::uint32_t mute_epoch,
                                 TimePoint now) noexcept {
    switch (mute_.apply(send_muted, mute_epoch)) {
        case MuteUpdate::kIgnored:
            return;
        case MuteUpdate::kMuted:
            state_ = next_signal_state(state_, SignalEvent::kSendMuted);
            return;
        case MuteUpdate::kUnmuted:
            state_ = next_signal_state(state_, SignalEvent::kSendUnmuted);
            resume_started_ = now;
            return;
    }
}

void SignalMonitor::poll(TimePoint now) noexcept {
    switch (state_) {
        case SignalState::kReceiving:
            if (now - last_frame_ >= kReceiveTimeout) {
                state_ = next_signal_state(state_, SignalEvent::kReceiveTimeout);
            }
            return;
        case SignalState::kResuming:
            if (now - resume_started_ >= kResumeTimeout) {
                state_ = next_signal_state(state_, SignalEvent::kResumeTimeout);
            }
            return;
        case SignalState::kPaused:
        case SignalState::kNoSignal:
            return;
    }
}

}  // namespace vocroz::transport
