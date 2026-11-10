// SPDX-License-Identifier: AGPL-3.0-only
//
// 返送モニターの再生（docs/adr/0036-return-monitor.md）。出力デバイスのオーディオスレッドが
// render() を呼び、返送のジッターバッファ（PlayoutStream）から取り出した PCM を、デバイスの
// すべてのチャンネルに書く。返送は Minus-Me のモノラルなので、ヘッドホンの両耳に同じ音を出す。
// 出力デバイスの抜去の通知を受け、コントロールスレッドが取り出せるようにする。
//
// ジッターバッファ、デコード、PLC、ドリフト補正は PlayoutStream が持ち、このクラスは
// デバイスのバッファの形に合わせるだけにする（オーディオパスに処理とバッファを足さない）。
#pragma once

#include <array>
#include <atomic>

#include "vocroz/audio_core/audio_buffer.h"
#include "vocroz/audio_core/ports/audio_device_listener.h"
#include "vocroz/audio_core/ports/audio_render_callback.h"
#include "vocroz/audio_core/realtime.h"
#include "vocroz/transport/playout_stream.h"

#include "client_constants.h"

namespace vocroz::client {

/// 返送の再生。
/// スレッド安全性: render() と on_device_lost() は出力デバイスのスレッドから、
/// take_device_lost() はコントロールスレッドから呼ぶ。同時に開く出力デバイスは 1 つだけにする
/// （PlayoutStream の消費者を 1 つに保つ）。
class ReturnPlayer final : public audio_core::AudioRenderCallback,
                           public audio_core::AudioDeviceListener {
public:
    /// @param returns 返送のジッターバッファ（モノラル）。このオブジェクトより長く生存させる
    explicit ReturnPlayer(transport::PlayoutStream& returns) noexcept : returns_(returns) {}

    /// 返送を out のすべてのチャンネルに書く。返送が届いていない間は無音を書く。
    /// オーディオスレッドから呼び出し可能。
    void render(audio_core::InterleavedBuffer out) noexcept VOCROZ_NONBLOCKING override;

    /// 出力デバイスが抜去された。ブロックしない。
    void on_device_lost() noexcept override {
        device_lost_.store(true, std::memory_order_release);
    }

    /// 抜去の通知があったかを返し、印を消す。
    [[nodiscard]] bool take_device_lost() noexcept {
        return device_lost_.exchange(false, std::memory_order_acq_rel);
    }

private:
    transport::PlayoutStream& returns_;
    std::array<float, kReturnRenderChunkFrames> mono_{};
    std::atomic<bool> device_lost_{false};
};

}  // namespace vocroz::client
