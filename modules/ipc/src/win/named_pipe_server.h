// SPDX-License-Identifier: AGPL-3.0-only
//
// pa-engine 側の名前付きパイプ。UI の接続を 1 つずつ受け付け、切断されたら次の接続を待つ。
// UI の再接続にまたがって同じ MessageChannel として見せ、session
// が接続と切断を意識せずに済むようにする （docs/adr/0020-ipc-pipe-and-webui-bridge.md）。
#pragma once

#include <memory>
#include <mutex>
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

/// pa-engine 側のパイプ。
/// 受信とリスナーの呼び出しは、待ち受けのスレッドで行う。
class NamedPipeServer final : public control_protocol::MessageChannel {
public:
    /// パイプを作り、待ち受けを始める。
    /// @param name   パイプの名前
    /// @param logger 接続と切断を記録するロガー
    /// @return 同じ名前のパイプがすでにあれば kAlreadyExists
    [[nodiscard]] static common::Result<std::unique_ptr<NamedPipeServer>, PipeError> create(
        const std::wstring& name, common::Logger& logger);

    /// 待ち受けを止め、つながっている UI を切断する。リスナーは呼ばない。
    ~NamedPipeServer() override;

    NamedPipeServer(const NamedPipeServer&) = delete;
    NamedPipeServer& operator=(const NamedPipeServer&) = delete;
    NamedPipeServer(NamedPipeServer&&) = delete;
    NamedPipeServer& operator=(NamedPipeServer&&) = delete;

    [[nodiscard]] common::Result<void, control_protocol::ChannelError> send(
        std::string_view message) override;
    void set_listener(control_protocol::MessageListener* listener) override;

private:
    /// 接続を待った結果。
    enum class WaitResult {
        kConnected,  ///< UI がつながった
        kRetry,      ///< つながった UI がすぐに切断した。もう一度待つ
        kStopped,    ///< 停止を要求された
        kFailed,     ///< 待ち受けを続けられない OS のエラー
    };

    NamedPipeServer(UniqueHandle pipe, UniqueHandle stop_event, UniqueHandle connect_event,
                    common::Logger& logger);

    void run();
    [[nodiscard]] WaitResult wait_for_client();

    const UniqueHandle pipe_;
    const UniqueHandle stop_event_;
    const UniqueHandle connect_event_;
    common::Logger& logger_;
    control_protocol::MessageListenerSlot listener_;

    std::mutex stream_mutex_;
    std::shared_ptr<NamedPipeStream> stream_;  ///< つながっている UI。いなければ nullptr
    bool stopping_ = false;

    std::thread thread_;
};

}  // namespace vocroz::ipc::win
