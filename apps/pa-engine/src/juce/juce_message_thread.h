// SPDX-License-Identifier: AGPL-3.0-only
//
// pa-engine 専用の JUCE のメッセージスレッド（docs/adr/0039-pa-engine-juce-message-thread.md）。
// JUCE 9 の MIDI は一覧、開閉、切断の通知をメッセージスレッドでしか扱わず、プラグインの
// エディターのウィンドウもメッセージスレッドを前提にする。pa-engine はコンソールのプロセスで
// JUCE のメッセージループを回していないので、1 本のスレッドで JUCE を初期化してループを回す。
// コントロールスレッドとは分け、エディターや MIDI のドライバーの処理と、UI のコマンドや
// 再接続の処理が互いを待たないようにする。
//
// JUCE の型は公開しない。コンポジションルートは std::function で処理を渡すだけにする。
#pragma once

#include <atomic>
#include <functional>
#include <thread>

namespace vocroz::engine {

/// JUCE のメッセージスレッド。JUCE の MessageManager はプロセスに 1 つなので、プロセスに 1
/// つだけ作る。 スレッド安全性: start() と stop() はコントロールスレッドから呼ぶ。post()
/// はどのスレッドからも 呼び出し可能（オーディオスレッドを除く）。
class JuceMessageThread {
public:
    /// メッセージスレッドで実行する処理。
    using Task = std::function<void()>;

    JuceMessageThread() = default;
    /// 動いていれば stop() する。
    ~JuceMessageThread();
    JuceMessageThread(const JuceMessageThread&) = delete;
    JuceMessageThread& operator=(const JuceMessageThread&) = delete;

    /// スレッドを立て、JUCE を初期化してメッセージループを回し始める。
    /// MessageManager ができて処理を積めるようになるまで待ってから戻る。動いていれば何もしない。
    void start();

    /// 処理をメッセージスレッドへ積む。積んだ順に実行する。
    /// @param task 実行する処理
    /// @return 積めたら true。動いていないか、止める途中なら false
    bool post(Task task);

    /// 最後の処理を実行してからループを抜け、JUCE を終了してスレッドを join する。
    /// JUCE の資源（MIDI のポート、エディター）は、JUCE を終える前にメッセージスレッドの上で
    /// 破棄する必要があるので、last で破棄する。動いていなければ何もしない。
    /// @param last メッセージスレッドで最後に実行する処理（空でもよい）
    void stop(const Task& last = {});

private:
    std::thread thread_;
    std::atomic<bool> running_{false};
};

}  // namespace vocroz::engine
