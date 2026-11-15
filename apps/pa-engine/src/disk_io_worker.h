// SPDX-License-Identifier: AGPL-3.0-only
//
// ディスク I/O スレッド（docs/adr/0043-recording-and-media-player.md）。
// 録音の書き出し（RecordingTrack::drain()、finish()）と、再生の先読み（PlaybackTrack::fill()）を
// 一定の間隔で回す。ファイルの読み書きはブロックしうるので、オーディオスレッドとも
// コントロールスレッドとも分ける。オーディオスレッドとはリングバッファだけで受け渡すので、
// 書き込みが遅れても、ディスクがいっぱいになっても、オーディオスレッドは待たない。
#pragma once

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace vocroz::engine {

/// ディスク I/O スレッドで回す仕事 1 つ（録音 1 回、再生ファイル 1 つ）。
/// スレッド安全性: service() はディスク I/O スレッドだけが呼ぶ。
class DiskJob {
public:
    virtual ~DiskJob() = default;

    /// 1 周期ぶんの読み書きをする。ブロッキング I/O を含んでよい。
    virtual void service() = 0;
};

/// ディスク I/O スレッド。
/// スレッド安全性: add()、remove()、start()、stop() はコントロールスレッドから呼ぶ。
/// run_once() はテストでスレッドを起こさずに 1 周期を回すためのもので、start() と併用しない。
class DiskIoWorker {
public:
    /// @param interval 仕事を回す間隔
    explicit DiskIoWorker(std::chrono::milliseconds interval);
    DiskIoWorker(const DiskIoWorker&) = delete;
    DiskIoWorker& operator=(const DiskIoWorker&) = delete;
    /// スレッドを止める（stop()）。
    ~DiskIoWorker();

    /// 仕事を加える。次の周期から回る。
    void add(std::shared_ptr<DiskJob> job);

    /// 仕事を外す。回している最中なら、その周期が終わってから外れる（呼び出しは待たない）。
    void remove(const std::shared_ptr<DiskJob>& job);

    /// スレッドを始める。始めていれば何もしない。
    void start();

    /// スレッドを止める。止める前に、残っている仕事を 1 周期ぶん回す（録音の最後のぶんを
    /// 書き出すため）。始めていなければ何もしない。
    void stop();

    /// 今の仕事を 1 周期ぶん回す。
    void run_once();

private:
    void loop();

    const std::chrono::milliseconds interval_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::vector<std::shared_ptr<DiskJob>> jobs_;
    bool stopping_ = false;
    std::thread thread_;
};

}  // namespace vocroz::engine
