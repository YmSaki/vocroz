// SPDX-License-Identifier: AGPL-3.0-only
//
// pa-engine の名前付きパイプにつなぐポート。コンポジションルートが ipc の
// connect_pipe_client() で実装し、テストではフェイクに差し替える。
// エンジンの起動と再アタッチの手順（EngineAttacher）を OS
// なしで確かめるため、接続をここに切り出す。
#pragma once

#include <memory>

#include "vocroz/common/result.h"
#include "vocroz/control_protocol/ports/message_channel.h"

namespace vocroz::pa_desktop {

/// エンジンにつなげなかった原因。
enum class ConnectError {
    kNotRunning,  ///< パイプがない（エンジンが動いていない、または起動の途中）
    kRefused,  ///< パイプはあるが使えない（ほかのユーザーのもの、なりすまし、OS のエラー）
};

/// エンジンのパイプへの接続。
class EngineConnector {
public:
    virtual ~EngineConnector() = default;

    /// パイプにつなぐ。待つ時間は短くし、呼び出し側が間隔を空けて試し直す。
    /// @return つながった経路。つながらなければその原因
    /// スレッド安全性: コントロールスレッド（メッセージスレッド）から呼ぶ。
    [[nodiscard]] virtual common::Result<std::unique_ptr<control_protocol::MessageChannel>,
                                         ConnectError>
    connect() = 0;
};

}  // namespace vocroz::pa_desktop
