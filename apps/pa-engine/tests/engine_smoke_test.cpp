// SPDX-License-Identifier: AGPL-3.0-only
//
// pa-engine の起動のスモークテスト（Windows だけ）。実行ファイルを別プロセスで起動し、
// 名前付きパイプでつないで、hello にスナップショットが返ること、UI が切断してつなぎ直しても
// エンジンが動き続けてスナップショットを送り直すこと、同じデータフォルダの 2 つ目の起動が
// 終了コード 3 で終わることを確かめる。出力デバイスは開かない（設定がないため）。

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <variant>

#include <gtest/gtest.h>
#include <windows.h>

#include "vocroz/control_protocol/json_codec.h"
#include "vocroz/control_protocol/ports/message_channel.h"
#include "vocroz/control_protocol/ports/message_listener.h"
#include "vocroz/ipc/factories.h"

#include "fakes/fake_logger.h"

namespace vocroz::engine {
namespace {

namespace cp = control_protocol;
using namespace std::chrono_literals;

// 起動を待つ時間。CI の Windows のランナーでも十分に間に合う長さにする。
constexpr auto kConnectTimeout = 10s;
constexpr auto kReplyTimeout = 10s;
constexpr DWORD kProcessTimeoutMs = 10000;

std::filesystem::path engine_path() {
    std::wstring buffer(MAX_PATH, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), MAX_PATH);
    buffer.resize(length);
    // テストの実行ファイルと pa-engine.exe は同じディレクトリに出力される。
    return std::filesystem::path(buffer).parent_path() / L"pa-engine.exe";
}

// 起動したプロセス。破棄時にまだ動いていれば強制終了する。
class EngineProcess {
public:
    explicit EngineProcess(const std::wstring& arguments) {
        std::wstring command = L"\"" + engine_path().wstring() + L"\" " + arguments;
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        started_ = CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                                  nullptr, &startup, &info_) != FALSE;
        if (started_) {
            CloseHandle(info_.hThread);
        }
    }
    EngineProcess(const EngineProcess&) = delete;
    EngineProcess& operator=(const EngineProcess&) = delete;
    ~EngineProcess() {
        if (!started_) {
            return;
        }
        if (WaitForSingleObject(info_.hProcess, 0) == WAIT_TIMEOUT) {
            TerminateProcess(info_.hProcess, 1);
            WaitForSingleObject(info_.hProcess, kProcessTimeoutMs);
        }
        CloseHandle(info_.hProcess);
    }

    [[nodiscard]] bool started() const {
        return started_;
    }

    // 終了を待って終了コードを返す。時間内に終わらなければ std::nullopt。
    [[nodiscard]] std::optional<DWORD> wait_exit() {
        if (WaitForSingleObject(info_.hProcess, kProcessTimeoutMs) != WAIT_OBJECT_0) {
            return std::nullopt;
        }
        DWORD code = 0;
        GetExitCodeProcess(info_.hProcess, &code);
        return code;
    }

    [[nodiscard]] bool running() const {
        return WaitForSingleObject(info_.hProcess, 0) == WAIT_TIMEOUT;
    }

private:
    bool started_ = false;
    PROCESS_INFORMATION info_{};
};

// 最初に届いたスナップショットを待つリスナー。
class SnapshotWaiter final : public cp::MessageListener {
public:
    void on_message(std::string_view message) override {
        auto decoded = cp::decode_engine_to_ui(message);
        if (!decoded || !std::holds_alternative<cp::MixerSnapshotEvent>(decoded.value())) {
            return;
        }
        const std::lock_guard lock(mutex_);
        snapshot_ = true;
        changed_.notify_all();
    }
    void on_closed() override {}

    [[nodiscard]] bool wait() {
        std::unique_lock lock(mutex_);
        return changed_.wait_for(lock, kReplyTimeout, [this] { return snapshot_; });
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    bool snapshot_ = false;
};

class EngineSmokeTest : public ::testing::Test {
protected:
    EngineSmokeTest()
        : data_dir_(std::filesystem::temp_directory_path() /
                    (L"vocroz-engine-smoke-" + std::to_wstring(GetCurrentProcessId()))) {
        std::filesystem::create_directories(data_dir_);
    }
    ~EngineSmokeTest() override {
        std::error_code ignored;
        std::filesystem::remove_all(data_dir_, ignored);
    }

    [[nodiscard]] std::wstring data_dir_argument() const {
        return L"\"--data-dir=" + data_dir_.wstring() + L"\"";
    }

    // つないで hello を送り、スナップショットが返るのを待つ。
    void expect_snapshot_on_hello() {
        const auto name = ipc::engine_pipe_name(data_dir_);
        ASSERT_TRUE(name.has_value());
        auto channel = ipc::connect_pipe_client(name.value(), kConnectTimeout, logger_);
        ASSERT_TRUE(channel.has_value());
        SnapshotWaiter waiter;
        channel.value()->set_listener(&waiter);
        ASSERT_TRUE(channel.value()->send(cp::encode_ui_to_engine(cp::Hello{})).has_value());
        EXPECT_TRUE(waiter.wait());
        channel.value()->set_listener(nullptr);
    }

    std::filesystem::path data_dir_;
    common::FakeLogger logger_;
};

TEST_F(EngineSmokeTest, PrintsVersionAndExitsZero) {
    EngineProcess engine(L"--version");
    ASSERT_TRUE(engine.started());
    EXPECT_EQ(engine.wait_exit(), DWORD{0});
}

TEST_F(EngineSmokeTest, RejectsUnknownArgumentWithUsageError) {
    EngineProcess engine(L"--unknown");
    ASSERT_TRUE(engine.started());
    EXPECT_EQ(engine.wait_exit(), DWORD{2});
}

TEST_F(EngineSmokeTest, SendsSnapshotAgainAfterUiReconnects) {
    EngineProcess engine(data_dir_argument());
    ASSERT_TRUE(engine.started());

    expect_snapshot_on_hello();
    // UI が落ちて（切断して）もエンジンは動き続け、つなぎ直した UI にスナップショットを送る。
    ASSERT_TRUE(engine.running());
    expect_snapshot_on_hello();
    EXPECT_TRUE(engine.running());
}

TEST_F(EngineSmokeTest, SecondInstanceForSameDataDirExitsWithAlreadyRunning) {
    EngineProcess first(data_dir_argument());
    ASSERT_TRUE(first.started());
    // 1 つ目がパイプを開くまで待ってから 2 つ目を起動する。
    expect_snapshot_on_hello();

    EngineProcess second(data_dir_argument());
    ASSERT_TRUE(second.started());
    EXPECT_EQ(second.wait_exit(), DWORD{3});
    EXPECT_TRUE(first.running());
}

}  // namespace
}  // namespace vocroz::engine
