// SPDX-License-Identifier: AGPL-3.0-only
//
// pa-engine のパイプへの接続。CreateFileW と WaitNamedPipeW を使う。

#include "win/named_pipe_client.h"

#include <algorithm>
#include <utility>

#include "ipc_constants.h"
#include "win/win32_security.h"

namespace vocroz::ipc::win {
namespace {

using Clock = std::chrono::steady_clock;
using control_protocol::ChannelError;

DWORD to_wait_millis(Clock::duration duration) {
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    return static_cast<DWORD>(std::max<long long>(millis, 1));
}

}  // namespace

common::Result<std::unique_ptr<NamedPipeClient>, PipeError> NamedPipeClient::connect(
    const std::wstring& name, std::chrono::milliseconds timeout, common::Logger& logger) {
    const auto sid = current_user_sid();
    if (!sid) {
        return common::make_unexpected(PipeError::kSystemError);
    }

    const auto deadline = Clock::now() + timeout;
    UniqueHandle pipe;
    while (true) {
        // SECURITY_IDENTIFICATION: サーバーにはこちらの身元の確認だけを許し、こちらの権限での
        // 動作（偽装）を許さない。名前を先取りした別のプロセスに権限を使わせないため。
        pipe = UniqueHandle{CreateFileW(
            name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr)};
        if (pipe.valid()) {
            break;
        }
        const DWORD error = GetLastError();
        const auto remaining = deadline - Clock::now();
        if (error == ERROR_ACCESS_DENIED) {
            return common::make_unexpected(PipeError::kAccessDenied);
        }
        if (error != ERROR_PIPE_BUSY && error != ERROR_FILE_NOT_FOUND) {
            return common::make_unexpected(PipeError::kSystemError);
        }
        if (remaining <= Clock::duration::zero()) {
            return common::make_unexpected(PipeError::kNotFound);
        }
        if (error == ERROR_PIPE_BUSY) {
            // ほかの UI がつながっている。空くか timeout になるまで待つ。
            WaitNamedPipeW(name.c_str(), to_wait_millis(remaining));
        } else {
            // エンジンがまだパイプを作っていない。WaitNamedPipeW はパイプがないとすぐに戻るので、
            // 間隔をあけて作り直す。
            std::this_thread::sleep_for(
                std::min<Clock::duration>(kConnectRetryInterval, remaining));
        }
    }

    // ACL では、エンジンが起動していない間にほかのユーザーが同じ名前で待ち受けるのを防げない。
    // 所有者が自分でなければ、コマンドを渡さずに切断する。
    const auto owner = owner_sid_of(pipe.get());
    if (!owner || *owner != *sid) {
        logger.log(common::LogLevel::kError,
                   "パイプの所有者がログイン中のユーザーではないため、接続をやめました");
        return common::make_unexpected(PipeError::kUntrustedServer);
    }
    return std::unique_ptr<NamedPipeClient>{new NamedPipeClient{std::move(pipe), logger}};
}

NamedPipeClient::NamedPipeClient(UniqueHandle pipe, common::Logger& logger)
    : pipe_(std::move(pipe)), logger_(logger), stream_(pipe_.get(), logger) {
    thread_ = std::thread{[this] { run(); }};
}

NamedPipeClient::~NamedPipeClient() {
    closing_by_owner_ = true;
    stream_.request_stop();
    thread_.join();
}

common::Result<void, ChannelError> NamedPipeClient::send(std::string_view message) {
    return stream_.send(message);
}

void NamedPipeClient::set_listener(control_protocol::MessageListener* listener) {
    listener_.set(listener);
}

void NamedPipeClient::run() {
    const auto reason = stream_.run_receiver(
        [this](std::string_view message) { listener_.deliver_message(message); });
    stream_.shutdown();
    if (closing_by_owner_) {
        return;
    }
    if (reason == NamedPipeStream::EndReason::kIoError) {
        logger_.log(common::LogLevel::kWarning, "パイプの読み込みに失敗したため切断しました");
    }
    logger_.log(common::LogLevel::kInfo, "エンジンとのパイプが切断されました");
    listener_.deliver_closed();
}

}  // namespace vocroz::ipc::win
