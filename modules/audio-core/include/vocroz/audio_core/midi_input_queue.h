// SPDX-License-Identifier: AGPL-3.0-only
//
// MIDI の入力 1 系統（MIDI デバイス 1 台、または子機 1 台）を、オーディオスレッドへ渡すキュー。
// 生産者のスレッド（MIDI デバイスのコールバック、ネットワーク受信スレッド）がタイムスタンプ付きの
// メッセージを積み、オーディオスレッドがブロックごとに取り出して、タイムスタンプをブロック内の
// サンプルオフセットに変換する。届いた順は変えない（Note On と Note Off の順序を保証する）。
// 規則は docs/adr/0029-midi-and-vsti.md に書いた。
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "vocroz/audio_core/midi_event.h"
#include "vocroz/audio_core/ports/midi_source.h"
#include "vocroz/audio_core/realtime.h"
#include "vocroz/common/ports/clock.h"
#include "vocroz/common/spsc_queue.h"

namespace vocroz::audio_core {

/// MidiInputQueue に積める、まだ取り出していないメッセージの数。
/// 鍵盤の和音の連打やコントローラーの連続送信でも、1 ブロック（約 2.7ms）の間に積まれるのは
/// 数十件なので、1 秒近く取り出されなくてもあふれない大きさにする。
inline constexpr std::size_t kMidiQueueCapacity = 1024;

/// タイムスタンプ付きの MIDI メッセージ。
struct TimestampedMidiMessage {
    /// 受け取った時刻（MIDI デバイスのタイムスタンプ、子機のメッセージを受信した時刻）。
    /// Mixer に注入する Clock と同じ基準の時刻にする。
    common::Clock::TimePoint timestamp;
    std::array<std::uint8_t, 3> data{};  ///< ステータスバイトとデータバイト
    std::uint8_t size = 0;               ///< data のうち有効なバイト数（1〜3）
};

/// MidiInputQueue の構成。
struct MidiInputQueueConfig {
    /// 発音を遅らせるサンプル数。0 ならイベントを届いたブロックの先頭に置き、遅延を最小にする
    /// （タイミングはブロック長ぶん揺れる）。デバイスのコールバック長にすると、タイミングの揺れが
    /// なくなる代わりに、遅延がその長さだけ増える。どちらにするかは session が入力ごとに決める。
    std::size_t schedule_ahead_samples = 0;
};

/// MIDI の入力 1 系統のキュー。
///
/// Panic: request_panic() を呼ぶと、次のブロックで未送信のメッセージを捨て、グラフに Panic
/// を求める。 グラフは、このソースの経路で発音中のノートすべてに Note Off を送る。 キューがあふれて
/// Note Off などの止める側のメッセージを落としたときも、音残りを防ぐために 自動で Panic
/// を求める（Note On を落としても音が鳴らないだけで、音は残らない）。
///
/// スレッド安全性: push() は生産者のスレッド 1 つだけ、pull() はオーディオスレッドだけが呼ぶ。
/// request_panic() はどのスレッドからも呼び出し可能。
class MidiInputQueue final : public MidiSource {
public:
    /// @param config 構成
    explicit MidiInputQueue(const MidiInputQueueConfig& config = {}) noexcept;
    MidiInputQueue(const MidiInputQueue&) = delete;
    MidiInputQueue& operator=(const MidiInputQueue&) = delete;
    MidiInputQueue(MidiInputQueue&&) = delete;
    MidiInputQueue& operator=(MidiInputQueue&&) = delete;
    ~MidiInputQueue() override = default;

    /// メッセージを積む。
    /// @param message 積むメッセージ。チャンネルメッセージ（is_channel_message()）でなければ捨てる
    /// @return 積めたら true。チャンネルメッセージでないか、キューが満杯なら false
    /// エラー時の振る舞い: 満杯で止める側のメッセージを落としたら、request_panic() と同じに扱う。
    /// スレッド安全性: 生産者のスレッド 1 つだけが呼ぶ。ブロックしない。
    [[nodiscard]] bool push(const TimestampedMidiMessage& message) noexcept VOCROZ_NONBLOCKING;

    /// Panic（All Notes Off）を求める。子機の LOST、MIDI デバイスの抜去で session が呼ぶ。
    /// スレッド安全性: どのスレッドからも呼び出し可能。ロックを取らず、ブロックしない。
    void request_panic() noexcept;

    /// このブロックで送るイベントを取り出す（MidiSource）。
    /// スレッド安全性: オーディオスレッドから呼び出し可能。
    [[nodiscard]] MidiPullResult pull(
        const MidiBlockClock& clock, std::span<MidiEvent> out) noexcept VOCROZ_NONBLOCKING override;

    /// あふれて落としたメッセージの累計を返す。診断のログと統計に使う。
    /// スレッド安全性: どのスレッドからも呼び出し可能。
    [[nodiscard]] std::uint64_t dropped_count() const noexcept;

private:
    const std::size_t schedule_ahead_samples_;
    common::SpscQueue<TimestampedMidiMessage, kMidiQueueCapacity> queue_;
    std::atomic<bool> panic_requested_{false};
    std::atomic<std::uint64_t> dropped_{0};
    // 次のブロック以降に鳴らすので持ち越したメッセージ。オーディオスレッドだけが触る。
    std::optional<TimestampedMidiMessage> held_;
};

}  // namespace vocroz::audio_core
