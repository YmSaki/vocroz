// SPDX-License-Identifier: AGPL-3.0-only
//
// オーディオデバイスの抜去や消失を、デバイスのアダプターから内側へ通知するリスナー。
// device は検知をイベントとして通知するだけで、ミュートなどの判断は受け取った側
// （session と Mixer::engage_safety_mute()）が行う。
#pragma once

namespace vocroz::audio_core {

/// オーディオデバイスの状態変化を受け取るリスナー。
class AudioDeviceListener {
public:
    virtual ~AudioDeviceListener() = default;

    /// 使用中のデバイスが抜去されたか、消失したことを通知する。
    /// 通知の後、そのデバイスのコールバックは呼ばれない。別のデバイスへの自動の切り替えはしない。
    /// スレッド安全性: デバイスのアダプターの任意のスレッドから呼ばれる。実装はブロックしない。
    virtual void on_device_lost() noexcept = 0;
};

}  // namespace vocroz::audio_core
