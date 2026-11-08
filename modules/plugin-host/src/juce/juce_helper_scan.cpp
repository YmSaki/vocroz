// SPDX-License-Identifier: AGPL-3.0-only
//
// scan_in_helper()（factories.h）の実装。JUCE の ChildProcess でヘルパーを起動し、
// 標準出力のスキャン結果を受け取る。
// ChildProcess の読み出しはヘルパーが終わるか出力を閉じるまでブロックするので、制限時間は
// 監視用のスレッドで測り、過ぎたらヘルパーを強制終了して読み出しを終わらせる。

#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

#include <juce_core/juce_core.h>

#include "vocroz/plugin_host/factories.h"

#include "plugin_host_constants.h"

namespace vocroz::plugin_host {
namespace {

using common::make_unexpected;

juce::String to_juce_string(const std::filesystem::path& path) {
    const auto utf8 = path.u8string();
    return juce::String::fromUTF8(reinterpret_cast<const char*>(utf8.c_str()),
                                  static_cast<int>(utf8.size()));
}

// 制限時間を過ぎたらプロセスを強制終了する監視役。破棄で監視を終える。
class Watchdog {
public:
    Watchdog(juce::ChildProcess& process, std::chrono::milliseconds timeout)
        : thread_([this, &process, timeout] {
              std::unique_lock lock(mutex_);
              if (!finished_changed_.wait_for(lock, timeout, [this] { return finished_; })) {
                  fired_ = true;
                  process.kill();
              }
          }) {}
    Watchdog(const Watchdog&) = delete;
    Watchdog& operator=(const Watchdog&) = delete;
    Watchdog(Watchdog&&) = delete;
    Watchdog& operator=(Watchdog&&) = delete;

    ~Watchdog() {
        stop();
    }

    // 監視を終える。制限時間を過ぎて強制終了していたら true を返す。
    bool stop() {
        {
            const std::lock_guard lock(mutex_);
            finished_ = true;
        }
        finished_changed_.notify_one();
        if (thread_.joinable()) {
            thread_.join();
        }
        return fired_;
    }

private:
    std::mutex mutex_;
    std::condition_variable finished_changed_;
    bool finished_ = false;
    std::atomic<bool> fired_ = false;
    std::thread thread_;  // 他のメンバーを使うので最後に宣言して最後に構築する
};

}  // namespace

common::Result<ScanReport, HelperScanError> scan_in_helper(const std::filesystem::path& helper,
                                                           const std::filesystem::path& plugin_file,
                                                           std::chrono::milliseconds timeout) {
    // POSIX では起動の失敗が子プロセスの中で起きて異常終了と区別できないので、先に確かめる。
    std::error_code error;
    if (!std::filesystem::is_regular_file(helper, error)) {
        return make_unexpected(HelperScanError::kLaunchFailed);
    }
    juce::StringArray args;
    args.add(to_juce_string(helper));
    args.add("--scan");
    args.add(to_juce_string(plugin_file));
    juce::ChildProcess process;
    if (!process.start(args, juce::ChildProcess::wantStdOut)) {
        return make_unexpected(HelperScanError::kLaunchFailed);
    }

    std::string output;
    bool oversized = false;
    bool timed_out = false;
    {
        Watchdog watchdog(process, timeout);
        std::array<char, 4096> chunk{};
        for (;;) {
            const int read =
                process.readProcessOutput(chunk.data(), static_cast<int>(chunk.size()));
            if (read <= 0) {
                break;
            }
            // 書き続ける不良ヘルパーで、メモリを使い尽くさないようにする。
            if (output.size() + static_cast<std::size_t>(read) > kMaxHelperOutputBytes) {
                oversized = true;
                process.kill();
                break;
            }
            output.append(chunk.data(), static_cast<std::size_t>(read));
        }
        // 出力を閉じたあとに止まるヘルパーも、監視役が制限時間で強制終了する。
        (void)process.waitForProcessToFinish(-1);
        timed_out = watchdog.stop();
    }

    if (timed_out) {
        return make_unexpected(HelperScanError::kTimedOut);
    }
    if (oversized) {
        return make_unexpected(HelperScanError::kInvalidOutput);
    }
    // ヘルパーは成功すると、プラグインが見つからなくても必ず結果を書く。
    // 何も書かずに終わったら、異常終了とみなす。
    // POSIX ではシグナルで落ちても終了コードが 0 に見えるので、出力の有無でも判定する。
    if (process.getExitCode() != 0 || output.empty()) {
        return make_unexpected(HelperScanError::kCrashed);
    }
    auto report = parse_scan_report(output);
    if (!report) {
        return make_unexpected(HelperScanError::kInvalidOutput);
    }
    return std::move(report).value();
}

}  // namespace vocroz::plugin_host
