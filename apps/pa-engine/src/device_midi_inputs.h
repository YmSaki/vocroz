// SPDX-License-Identifier: AGPL-3.0-only
//
// 親機につないだ MIDI デバイスの入力をまとめて持つ（docs/adr/0029-midi-and-vsti.md、
// docs/adr/0039-pa-engine-juce-message-thread.md）。
//
// 使える MIDI 入力ポートをすべて開き、ポートごとに MidiInputQueue を 1 つ置いて、届いた
// メッセージをそのまま積む。キューはリスナー経由でグラフの MIDI ソースになる。
// どのポートを使うかを選ぶ画面はまだないので、使えるものは全部開く。グラフに MIDI の経路
// （プラグインへの結線）がなければ、積んだメッセージはグラフが取り出して捨てるだけで、音には
// 影響しない。
//
// 抜去の通知を受けたら、そのキューに Panic を求め、グラフから外すよう知らせる。別のポートへは
// 切り替えない。同じポートが一覧に戻ったら、次の refresh() で新しいキューで開き直す。
//
// JUCE の MIDI は一覧、開閉、抜去の通知を JUCE のメッセージスレッドでしか扱わないので、
// このクラスもメッセージスレッドだけで使う。届いたメッセージは JUCE の受信スレッドから
// キューへ積み、メッセージスレッドを経由しない（打鍵からオーディオスレッドまでの遅延を増やさない）。
#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>

#include "vocroz/audio_core/midi_device_info.h"
#include "vocroz/audio_core/midi_input_queue.h"
#include "vocroz/audio_core/ports/midi_device_catalog.h"
#include "vocroz/audio_core/ports/midi_input.h"
#include "vocroz/common/ports/logger.h"

namespace vocroz::engine {

/// MIDI デバイスのキューを開いたり外したりしたことの通知。
/// スレッド安全性: JUCE のメッセージスレッドから呼ばれる。
class DeviceMidiListener {
public:
    virtual ~DeviceMidiListener() = default;

    /// ポートを開いた。キューをグラフの MIDI ソースにする。
    /// @param device_id ポートの ID（MidiDeviceInfo::id）
    /// @param queue     そのポートのキュー
    virtual void on_midi_device_opened(const std::string& device_id,
                                       std::shared_ptr<audio_core::MidiInputQueue> queue) = 0;

    /// ポートが抜去されたか、一覧から消えた。キューには Panic を求め済み。グラフから外す。
    /// @param device_id ポートの ID
    virtual void on_midi_device_closed(const std::string& device_id) = 0;
};

/// 親機の MIDI デバイスの入力。
/// スレッド安全性: すべてのメンバー関数を JUCE のメッセージスレッドから呼ぶ。
class DeviceMidiInputs {
public:
    /// ポートの ID から MIDI 入力ポートを作る関数。
    using Opener =
        std::function<std::unique_ptr<audio_core::MidiInput>(const audio_core::MidiDeviceInfo&)>;

    /// 注入する依存。どれもこのオブジェクトより長く生存させる（AGENTS.md 4.3）。
    struct Ports {
        audio_core::MidiDeviceCatalog& catalog;  ///< 使える MIDI 入力ポートの一覧
        Opener open;                             ///< ポートの作り方
        DeviceMidiListener& listener;            ///< キューの開閉の通知先
        common::Logger& logger;
    };

    /// @param ports 注入する依存
    explicit DeviceMidiInputs(Ports ports);
    /// 開いているポートをすべて閉じる（close_all()）。
    ~DeviceMidiInputs();
    DeviceMidiInputs(const DeviceMidiInputs&) = delete;
    DeviceMidiInputs& operator=(const DeviceMidiInputs&) = delete;

    /// 一覧を読み直し、新しいポートを開き、抜去されたか一覧から消えたポートを閉じる。
    /// 開けなかったポートは、一覧からいったん消えるまで開き直さない（他のアプリが排他で使って
    /// いるポートを周期ごとに開こうとして、ログを埋めないため）。
    void refresh();

    /// 開いているポートをすべて閉じる。キューに Panic を求めてから閉じる。
    /// 終了のとき、JUCE を終える前に呼ぶ。
    void close_all();

    /// 開いているポートの数を返す。
    [[nodiscard]] std::size_t open_count() const noexcept;

private:
    struct Port;
    using PortMap = std::map<std::string, std::unique_ptr<Port>, std::less<>>;

    // キューに Panic を求めてからポートを閉じる。
    void close(PortMap::iterator port);

    Ports ports_;
    PortMap open_;
    // 開けなかったポートの ID。一覧から消えたら外す。
    std::set<std::string, std::less<>> failed_;
};

}  // namespace vocroz::engine
