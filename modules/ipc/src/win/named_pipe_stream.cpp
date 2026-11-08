// SPDX-License-Identifier: AGPL-3.0-only
//
// 名前付きパイプ 1 本の送受信。OVERLAPPED の ReadFile と WriteFile を、停止のイベントと
// 一緒に待つことで、読み書きの途中でも停止の要求に応じられるようにする。

#include "win/named_pipe_stream.h"

#include <array>
#include <span>
#include <vector>

#include "vocroz/ipc/frame_codec.h"

#include "ipc_constants.h"

namespace vocroz::ipc::win {
namespace {

using control_protocol::ChannelError;

// OVERLAPPED の I/O の完了か停止のイベントを待つ。停止なら I/O を取り消して完了を待つ。
// @return I/O が完了したら true、停止を要求されたら false
bool wait_io_or_stop(HANDLE pipe, OVERLAPPED& overlapped, HANDLE stop_event) {
    const std::array<HANDLE, 2> handles{overlapped.hEvent, stop_event};
    const DWORD result =
        WaitForMultipleObjects(static_cast<DWORD>(handles.size()), handles.data(), FALSE, INFINITE);
    if (result == WAIT_OBJECT_0) {
        return true;
    }
    // 取り消した I/O も、カーネルがバッファへの書き込みを終えるまで待つ。
    // 待たずに戻ると、スタックの OVERLAPPED とバッファが解放された後に書き込まれうる。
    CancelIoEx(pipe, &overlapped);
    DWORD ignored = 0;
    GetOverlappedResult(pipe, &overlapped, &ignored, TRUE);
    return false;
}

bool is_disconnect_error(DWORD error) {
    return error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED ||
           error == ERROR_NO_DATA;
}

}  // namespace

NamedPipeStream::NamedPipeStream(HANDLE pipe, common::Logger& logger)
    : pipe_(pipe), logger_(logger), stop_event_(make_manual_reset_event()) {
    sender_ = std::thread{[this] { run_sender(); }};
}

NamedPipeStream::~NamedPipeStream() {
    shutdown();
}

common::Result<void, ChannelError> NamedPipeStream::send(std::string_view message) {
    auto frame = encode_frame(message);
    if (!frame) {
        return common::make_unexpected(ChannelError::kTooLarge);
    }
    bool overflowed = false;
    {
        const std::lock_guard lock{mutex_};
        if (stopping_) {
            return common::make_unexpected(ChannelError::kClosed);
        }
        if (queued_bytes_ + frame.value().size() > kMaxQueuedBytes) {
            overflowed = true;
        } else {
            queued_bytes_ += frame.value().size();
            queue_.push_back(std::move(frame).value());
        }
    }
    if (overflowed) {
        // 相手がハングして受信していない。待ち続けると送信側のメモリが増え続けるので、接続を切る。
        // 相手は復帰したら再接続し、スナップショットから表示し直す。
        logger_.log(common::LogLevel::kWarning,
                    "パイプの相手が受信していないため、送信キューがあふれて接続を切りました");
        request_stop();
        return common::make_unexpected(ChannelError::kClosed);
    }
    queue_changed_.notify_one();
    return {};
}

NamedPipeStream::EndReason NamedPipeStream::run_receiver(const MessageSink& on_message) {
    const UniqueHandle read_event = make_manual_reset_event();
    if (!read_event.valid() || !stop_event_.valid()) {
        return EndReason::kIoError;
    }
    std::vector<std::byte> buffer(kReadChunkBytes);
    FrameDecoder decoder;

    while (WaitForSingleObject(stop_event_.get(), 0) != WAIT_OBJECT_0) {
        OVERLAPPED overlapped{};
        overlapped.hEvent = read_event.get();
        DWORD read = 0;
        if (ReadFile(pipe_, buffer.data(), static_cast<DWORD>(buffer.size()), nullptr,
                     &overlapped) == FALSE) {
            const DWORD error = GetLastError();
            if (is_disconnect_error(error)) {
                return EndReason::kDisconnected;
            }
            if (error != ERROR_IO_PENDING) {
                return EndReason::kIoError;
            }
            if (!wait_io_or_stop(pipe_, overlapped, stop_event_.get())) {
                return EndReason::kStopped;
            }
        }
        if (GetOverlappedResult(pipe_, &overlapped, &read, FALSE) == FALSE) {
            return is_disconnect_error(GetLastError()) ? EndReason::kDisconnected
                                                       : EndReason::kIoError;
        }
        const auto fed = decoder.feed(std::span{buffer}.first(read), on_message);
        if (!fed) {
            logger_.log(common::LogLevel::kWarning,
                        "パイプで長さが上限を超えるフレームを受け取ったため、接続を切りました");
            return EndReason::kProtocolError;
        }
    }
    return EndReason::kStopped;
}

void NamedPipeStream::request_stop() noexcept {
    {
        const std::lock_guard lock{mutex_};
        stopping_ = true;
    }
    queue_changed_.notify_all();
    SetEvent(stop_event_.get());
}

void NamedPipeStream::shutdown() noexcept {
    request_stop();
    std::call_once(join_once_, [this] {
        if (sender_.joinable()) {
            sender_.join();
        }
    });
}

void NamedPipeStream::run_sender() {
    const UniqueHandle write_event = make_manual_reset_event();
    if (!write_event.valid() || !stop_event_.valid()) {
        request_stop();
        return;
    }
    while (true) {
        std::string frame;
        {
            std::unique_lock lock{mutex_};
            queue_changed_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_) {
                return;
            }
            frame = std::move(queue_.front());
            queue_.pop_front();
        }
        const bool written = write_frame(frame, write_event.get());
        {
            // 書き込み中のフレームも未送信として数え、相手が読まないまま積み上がるのを検出する。
            const std::lock_guard lock{mutex_};
            queued_bytes_ -= frame.size();
        }
        if (!written) {
            request_stop();
            return;
        }
    }
}

bool NamedPipeStream::write_frame(const std::string& frame, HANDLE write_event) {
    OVERLAPPED overlapped{};
    overlapped.hEvent = write_event;
    DWORD written = 0;
    if (WriteFile(pipe_, frame.data(), static_cast<DWORD>(frame.size()), nullptr, &overlapped) ==
        FALSE) {
        const DWORD error = GetLastError();
        if (error != ERROR_IO_PENDING) {
            if (!is_disconnect_error(error)) {
                logger_.log(common::LogLevel::kWarning, "パイプへの書き込みに失敗しました");
            }
            return false;
        }
        if (!wait_io_or_stop(pipe_, overlapped, stop_event_.get())) {
            return false;
        }
    }
    if (GetOverlappedResult(pipe_, &overlapped, &written, FALSE) == FALSE ||
        written != frame.size()) {
        return false;
    }
    return true;
}

}  // namespace vocroz::ipc::win
