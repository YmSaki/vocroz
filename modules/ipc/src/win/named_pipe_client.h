// SPDX-License-Identifier: AGPL-3.0-only
//
// remote-pa（親機モード）から pa-engine のパイプへの 1 つの接続。
// 切断されたら on_closed() を 1 回呼び、以降は使わない。つながり直すときは作り直す
// （docs/adr/0020-ipc-pipe-and-webui-bridge.md）。
#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include <windows.h>

#include "vocroz/common/ports/logger.h"
#include "vocroz/common/result.h"
#include "vocroz/control_protocol/listener_slot.h"
#include "vocroz/control_protocol/ports/message_channel.h"
#include "vocroz/ipc/factories.h"

#include "win/named_pipe_stream.h"
#include "win/win32_handle.h"

namespace vocroz::ipc::win {

/// pa-engine のパイプへの接続。
/// 受信とリスナーの呼び出しは、接続ごとの受信スレッドで行う。
class NamedPipeClient final : public control_protocol::MessageChannel {
public:
    /// パイプに接続する。パイプがないかビジーの間は timeout まで再試行する。
    /// 接続したら、パイプの所有者がログイン中ユーザーであることを確かめる。
    /// @return 所有者が違えば kUntrustedServer、待っても接続できなければ kNotFound
    [[nodiscard]] static common::Result<std::unique_ptr<NamedPipeClient>, PipeError> connect(
        const std::wstring& name, std::chrono::milliseconds timeout, common::Logger& logger);

    /// 接続を閉じる。リスナーの on_closed() は呼ばない。
    ~NamedPipeClient() override;

    NamedPipeClient(const NamedPipeClient&) = delete;
    NamedPipeClient& operator=(const NamedPipeClient&) = delete;
    NamedPipeClient(NamedPipeClient&&) = delete;
    NamedPipeClient& operator=(NamedPipeClient&&) = delete;

    [[nodiscard]] common::Result<void, control_protocol::ChannelError> send(
        std::string_view message) override;
    void set_listener(control_protocol::MessageListener* listener) override;

private:
    NamedPipeClient(UniqueHandle pipe, common::Logger& logger);

    void run();

    // 宣言の逆順に破棄されるので、パイプはストリームより後に閉じられる。
    const UniqueHandle pipe_;
    common::Logger& logger_;
    control_protocol::MessageListenerSlot listener_;
    NamedPipeStream stream_;
    std::atomic<bool> closing_by_owner_{false};
    std::thread thread_;
};

}  // namespace vocroz::ipc::win
