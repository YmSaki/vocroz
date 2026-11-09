// SPDX-License-Identifier: AGPL-3.0-only
//
// 子機から届く MIDI（信頼制御層の MIDI レーン）を、承認済みの子機のぶんだけ受け付け、
// 子機ごとの MidiInputQueue に積む（docs/adr/0029-midi-and-vsti.md）。
// MIDI は打鍵の遅れがそのまま聞こえるので、コントロールスレッドを経由せず、ネットワーク受信
// スレッドで直接デコードしてキューに積む。承認と途絶の判断はコントロールスレッドの HostSession と
// 同じ通知から受ける。
#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>

#include "vocroz/audio_core/midi_input_queue.h"
#include "vocroz/common/ports/clock.h"
#include "vocroz/session/identifiers.h"

namespace vocroz::session {

class MidiTimeline;

/// 子機の MIDI の受け口。キューはチャンネルストリップごとに 1 つ持ち、接続をストリップへ結び付けて
/// 受け付ける。再接続で接続が変わっても同じストリップなら同じキューを使うので、グラフの MIDI
/// ソースを 差し替えずに済む。
///
/// - 承認前の接続と、閉じた接続の MIDI は捨てる（承認前遮断）
/// - 回線の途絶（RECONNECTING）、接続の終了、ストリップの削除では Panic を求め、発音中のノートを
///   止める。途絶の時点で止めるのは、そのまま復帰しなければ再接続の猶予のあいだ音が鳴り続けるため
///
/// コンポジションルートは、PeerLink の admit() と close()、MixerControl の remove_channel()、
/// 受信の途絶と復帰の通知を、それぞれ admit()、close()、remove_strip()、suspend()、resume() へ
/// 渡す。どれも HostSession が承認と切断を判断した結果なので、承認の規則はここに重複しない。
///
/// スレッド安全性: on_midi_message() はネットワーク受信スレッド（1 つ）だけ、それ以外は
/// コントロールスレッドから呼ぶ。内部の対応表はミューテックスで守る（どちらのスレッドも
/// オーディオスレッドではないのでロックを取ってよい。AGENTS.md 2.3）。
class PeerMidiInbox {
public:
    /// @param clock 受信時刻を読む時計。Mixer に注入するものと同じ基準にする
    explicit PeerMidiInbox(const common::Clock& clock);
    ~PeerMidiInbox();

    PeerMidiInbox(const PeerMidiInbox&) = delete;
    PeerMidiInbox& operator=(const PeerMidiInbox&) = delete;

    /// 承認した接続をストリップへ結び付け、その MIDI を受け付け始める。ストリップのキューが
    /// なければ作る。同じストリップに前の接続が結び付いていれば外す（再接続）。時刻の写し方は
    /// 最初からやり直す（再接続で子機の時計が変わりうるため）。
    /// @param peer     承認した接続
    /// @param strip_id 流し込むチャンネルストリップの entity_id
    /// @param config   キューを新しく作るときの構成（schedule_ahead_samples）
    /// @return そのストリップの MIDI のキュー。グラフの MIDI ソースとして渡す
    /// スレッド安全性: コントロールスレッドから呼ぶ。
    std::shared_ptr<audio_core::MidiInputQueue> admit(
        PeerHandle peer, const std::string& strip_id,
        const audio_core::MidiInputQueueConfig& config = {});

    /// 回線が途絶えた。Panic を求め、復帰するまでこの接続の MIDI を捨てる。
    /// @param peer 接続
    /// スレッド安全性: コントロールスレッドから呼ぶ。
    void suspend(PeerHandle peer);

    /// 途絶えていた回線が同じ接続のまま復帰した。MIDI の受け付けを再開する。
    /// @param peer 接続
    /// スレッド安全性: コントロールスレッドから呼ぶ。
    void resume(PeerHandle peer);

    /// 接続を閉じた。Panic を求め、結び付きを外す。ストリップのキューは残す
    /// （ストリップと MIDI の経路は残り、再接続で同じキューを使うため）。
    /// @param peer 接続。結び付いていなければ何もしない
    /// スレッド安全性: コントロールスレッドから呼ぶ。
    void close(PeerHandle peer);

    /// ストリップを削除した。Panic を求めてからキューを手放す。グラフがまだキューを参照していても、
    /// Panic を受け取ってから外れる。
    /// @param strip_id チャンネルストリップの entity_id
    /// スレッド安全性: コントロールスレッドから呼ぶ。
    void remove_strip(const std::string& strip_id);

    /// ストリップの MIDI のキューを返す。グラフを組み立てるときに使う。
    /// @param strip_id チャンネルストリップの entity_id
    /// @return キュー。まだ一度も承認していないストリップなら nullptr
    /// スレッド安全性: コントロールスレッドから呼ぶ。
    [[nodiscard]] std::shared_ptr<audio_core::MidiInputQueue> queue_for(
        const std::string& strip_id);

    /// MIDI レーンのメッセージを受け取る。不正な形式、承認していない接続、途絶中の接続の
    /// メッセージは捨てる。
    /// @param peer 届いた接続（認証済み）
    /// @param message MIDI レーンのメッセージの本文
    /// スレッド安全性: ネットワーク受信スレッド（1 つ）だけが呼ぶ。
    void on_midi_message(PeerHandle peer, std::span<const std::uint8_t> message);

    /// 捨てたメッセージの累計を返す（不正な形式と、受け付けていない接続のぶん）。診断に使う。
    /// スレッド安全性: どのスレッドからも呼び出し可能。
    [[nodiscard]] std::uint64_t rejected_count() const noexcept;

private:
    struct Strip;

    void unbind(Strip& strip);

    const common::Clock& clock_;
    std::mutex mutex_;
    std::map<std::string, std::unique_ptr<Strip>, std::less<>> strips_;
    std::map<PeerHandle, Strip*> peers_;
    std::atomic<std::uint64_t> rejected_{0};
};

}  // namespace vocroz::session
