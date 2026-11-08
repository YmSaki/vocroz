// SPDX-License-Identifier: AGPL-3.0-only
//
// pa-engine 側の名前付きパイプ。CreateNamedPipeW と OVERLAPPED の ConnectNamedPipe を使う。

#include "win/named_pipe_server.h"

#include <array>
#include <utility>

#include "ipc_constants.h"
#include "win/win32_security.h"

namespace vocroz::ipc::win {
namespace {

using control_protocol::ChannelError;

}  // namespace

common::Result<std::unique_ptr<NamedPipeServer>, PipeError> NamedPipeServer::create(
    const std::wstring& name, common::Logger& logger) {
    const auto sid = current_user_sid();
    if (!sid) {
        return common::make_unexpected(PipeError::kSystemError);
    }
    auto security = OwnerOnlySecurity::create(*sid);
    if (!security) {
        return common::make_unexpected(PipeError::kSystemError);
    }

    // FILE_FLAG_FIRST_PIPE_INSTANCE: 同じ名前のパイプがすでにあれば失敗させ、ほかのプロセスが
    // 先に待ち受けている（名前の先取り）ときにエンジンを起動させない。
    // PIPE_REJECT_REMOTE_CLIENTS: ネットワーク越しの接続を拒否する。
    // インスタンスを 1 つにして、同時につながる UI を 1 つに限る。
    UniqueHandle pipe{CreateNamedPipeW(
        name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1,
        static_cast<DWORD>(kPipeBufferBytes), static_cast<DWORD>(kPipeBufferBytes), 0,
        security->attributes())};
    if (!pipe.valid()) {
        const DWORD error = GetLastError();
        // 既存のインスタンスがあると、FILE_FLAG_FIRST_PIPE_INSTANCE は ERROR_ACCESS_DENIED、
        // インスタンス数の上限は ERROR_PIPE_BUSY で失敗する。どちらも同じ名前のパイプがある。
        if (error == ERROR_ACCESS_DENIED || error == ERROR_PIPE_BUSY) {
            return common::make_unexpected(PipeError::kAlreadyExists);
        }
        return common::make_unexpected(PipeError::kSystemError);
    }
    UniqueHandle stop_event = make_manual_reset_event();
    UniqueHandle connect_event = make_manual_reset_event();
    if (!stop_event.valid() || !connect_event.valid()) {
        return common::make_unexpected(PipeError::kSystemError);
    }
    return std::unique_ptr<NamedPipeServer>{new NamedPipeServer{
        std::move(pipe), std::move(stop_event), std::move(connect_event), logger}};
}

NamedPipeServer::NamedPipeServer(UniqueHandle pipe, UniqueHandle stop_event,
                                 UniqueHandle connect_event, common::Logger& logger)
    : pipe_(std::move(pipe)),
      stop_event_(std::move(stop_event)),
      connect_event_(std::move(connect_event)),
      logger_(logger) {
    thread_ = std::thread{[this] { run(); }};
}

NamedPipeServer::~NamedPipeServer() {
    {
        const std::lock_guard lock{stream_mutex_};
        stopping_ = true;
        if (stream_) {
            stream_->request_stop();
        }
    }
    SetEvent(stop_event_.get());
    thread_.join();
}

common::Result<void, ChannelError> NamedPipeServer::send(std::string_view message) {
    std::shared_ptr<NamedPipeStream> stream;
    {
        const std::lock_guard lock{stream_mutex_};
        stream = stream_;
    }
    if (!stream) {
        return common::make_unexpected(ChannelError::kClosed);
    }
    return stream->send(message);
}

void NamedPipeServer::set_listener(control_protocol::MessageListener* listener) {
    listener_.set(listener);
}

void NamedPipeServer::run() {
    while (true) {
        const WaitResult waited = wait_for_client();
        if (waited == WaitResult::kStopped) {
            return;
        }
        if (waited == WaitResult::kFailed) {
            logger_.log(common::LogLevel::kError, "UI の接続の待ち受けを続けられなくなりました");
            listener_.deliver_closed();
            return;
        }
        if (waited == WaitResult::kRetry) {
            DisconnectNamedPipe(pipe_.get());
            continue;
        }

        auto stream = std::make_shared<NamedPipeStream>(pipe_.get(), logger_);
        {
            const std::lock_guard lock{stream_mutex_};
            if (stopping_) {
                return;
            }
            stream_ = stream;
        }
        logger_.log(common::LogLevel::kInfo, "UI がパイプに接続しました");
        const auto reason = stream->run_receiver(
            [this](std::string_view message) { listener_.deliver_message(message); });
        {
            const std::lock_guard lock{stream_mutex_};
            stream_.reset();
        }
        // 送信スレッドを止めてから切断する。止める前に次の UI とつながると、前の接続あての
        // メッセージが次の UI に書き込まれうる。
        stream->shutdown();
        DisconnectNamedPipe(pipe_.get());
        if (reason == NamedPipeStream::EndReason::kIoError) {
            logger_.log(common::LogLevel::kWarning,
                        "パイプの読み込みに失敗したため UI を切断しました");
        }
        logger_.log(common::LogLevel::kInfo, "UI がパイプから切断されました");
    }
}

NamedPipeServer::WaitResult NamedPipeServer::wait_for_client() {
    OVERLAPPED overlapped{};
    overlapped.hEvent = connect_event_.get();
    ResetEvent(connect_event_.get());
    if (ConnectNamedPipe(pipe_.get(), &overlapped) != FALSE) {
        return WaitResult::kConnected;
    }
    switch (GetLastError()) {
        case ERROR_PIPE_CONNECTED:
            // ConnectNamedPipe を呼ぶ前に UI がつながっていた。
            return WaitResult::kConnected;
        case ERROR_NO_DATA:
            // つながった UI が、読み書きの前に切断した。
            return WaitResult::kRetry;
        case ERROR_IO_PENDING:
            break;
        default:
            return WaitResult::kFailed;
    }

    const std::array<HANDLE, 2> handles{connect_event_.get(), stop_event_.get()};
    const DWORD result =
        WaitForMultipleObjects(static_cast<DWORD>(handles.size()), handles.data(), FALSE, INFINITE);
    DWORD ignored = 0;
    if (result != WAIT_OBJECT_0) {
        CancelIoEx(pipe_.get(), &overlapped);
        GetOverlappedResult(pipe_.get(), &overlapped, &ignored, TRUE);
        return WaitResult::kStopped;
    }
    if (GetOverlappedResult(pipe_.get(), &overlapped, &ignored, FALSE) == FALSE) {
        return GetLastError() == ERROR_NO_DATA ? WaitResult::kRetry : WaitResult::kFailed;
    }
    return WaitResult::kConnected;
}

}  // namespace vocroz::ipc::win
