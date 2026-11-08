// SPDX-License-Identifier: AGPL-3.0-only
//
// 接続済みの名前付きパイプ 1 本の上で、長さ付き JSON を送受信する。
// サーバーとクライアントの両方が使う。送信は送信キューと送信スレッドで非同期にし、
// 相手が受信しなくなっても send()
// の呼び出し元を止めない（docs/adr/0020-ipc-pipe-and-webui-bridge.md）。
#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

#include <windows.h>

#include "vocroz/common/ports/logger.h"
#include "vocroz/common/result.h"
#include "vocroz/control_protocol/ports/message_channel.h"

#include "win/win32_handle.h"

namespace vocroz::ipc::win {

/// 接続済みのパイプ 1 本の送受信。
/// パイプの HANDLE は借りるだけで、閉じるのは所有者（サーバーかクライアント）。
/// HANDLE は FILE_FLAG_OVERLAPPED で開いたものに限る。
class NamedPipeStream {
public:
    /// run_receiver() が戻った理由。
    enum class EndReason {
        kDisconnected,   ///< 相手が切断した
        kProtocolError,  ///< 長さが上限を超えるフレームを受け取った
        kStopped,  ///< request_stop() が呼ばれた（送信キューのあふれと送信の失敗を含む）
        kIoError,  ///< そのほかの読み込みの失敗
    };

    /// 本文を受け取る関数。
    using MessageSink = std::function<void(std::string_view message)>;

    /// 送信スレッドを開始する。
    /// @param pipe   接続済みのパイプ。このオブジェクトより長く開いておく
    /// @param logger 送信の失敗とあふれを記録するロガー
    NamedPipeStream(HANDLE pipe, common::Logger& logger);

    /// 停止を要求し、送信スレッドの終了を待つ。run_receiver() は戻っていること。
    ~NamedPipeStream();

    NamedPipeStream(const NamedPipeStream&) = delete;
    NamedPipeStream& operator=(const NamedPipeStream&) = delete;
    NamedPipeStream(NamedPipeStream&&) = delete;
    NamedPipeStream& operator=(NamedPipeStream&&) = delete;

    /// メッセージを送信キューに積む。
    /// 未送信のバイト数が上限を超えたら、相手が受信していないとみなして停止し、kClosed を返す。
    /// スレッド安全性: どのスレッドからも同時に呼び出し可能。ブロックしない。
    [[nodiscard]] common::Result<void, control_protocol::ChannelError> send(
        std::string_view message);

    /// 受信を続け、本文ごとに on_message を呼ぶ。切断、不正なフレーム、停止の要求で戻る。
    /// スレッド安全性: 1 つの受信スレッドからだけ、1 回だけ呼ぶ。on_message
    /// はそのスレッドで呼ばれる。
    [[nodiscard]] EndReason run_receiver(const MessageSink& on_message);

    /// 送受信の停止を要求する。実行中の読み書きは取り消される。以降の send() は kClosed
    /// で失敗する。 スレッド安全性: どのスレッドからも呼び出し可能。何度呼んでもよい。
    void request_stop() noexcept;

    /// 停止を要求し、送信スレッドの終了を待つ。戻った後は、このオブジェクトはパイプに書き込まない。
    /// パイプを別の接続に使い回す前（サーバーの DisconnectNamedPipe の前）に呼ぶ。
    /// スレッド安全性: 送信スレッド以外のどのスレッドからも呼び出し可能。何度呼んでもよい。
    void shutdown() noexcept;

private:
    void run_sender();

    /// 1 フレームを書き込む。停止を要求されたか、書き込みに失敗したら false。
    [[nodiscard]] bool write_frame(const std::string& frame, HANDLE write_event);

    const HANDLE pipe_;
    common::Logger& logger_;
    const UniqueHandle stop_event_;

    std::mutex mutex_;
    std::condition_variable queue_changed_;
    std::deque<std::string> queue_;
    std::size_t queued_bytes_ = 0;
    bool stopping_ = false;

    std::once_flag join_once_;
    std::thread sender_;
};

}  // namespace vocroz::ipc::win
