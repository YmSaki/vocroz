// SPDX-License-Identifier: AGPL-3.0-only
//
// UI からのメッセージの受け口。名前付きパイプの受信スレッドでデコード（信頼境界の検証）し、
// 検証済みのメッセージだけをコントロールスレッドへ移して session に渡す。
// デコードは状態を持たないので受信スレッドで行い、コントロールスレッドの負荷と、
// 不正なメッセージを積む量を減らす。
#pragma once

#include <functional>
#include <string_view>

#include "vocroz/common/ports/logger.h"
#include "vocroz/control_protocol/commands.h"
#include "vocroz/control_protocol/ports/message_listener.h"

#include "control_loop.h"

namespace vocroz::engine {

/// UI からのメッセージの受け口。
/// スレッド安全性: on_message() と on_closed() は経路の受信スレッドから呼ばれる。
/// handler はコントロールスレッドで呼ぶ。
class UiMessageReceiver final : public control_protocol::MessageListener {
public:
    /// 検証済みのメッセージを受け取る処理。コントロールスレッドで呼ぶ。
    using Handler = std::function<void(const control_protocol::UiToEngineMessage&)>;

    /// @param loop    コントロールスレッドのループ。このオブジェクトより長く生存させる
    /// @param handler 検証済みのメッセージを受け取る処理
    /// @param logger  不正なメッセージと経路の異常の記録先。このオブジェクトより長く生存させる
    UiMessageReceiver(ControlLoop& loop, Handler handler, common::Logger& logger)
        : loop_(loop), handler_(std::move(handler)), logger_(logger) {}

    /// メッセージをデコードし、正しければコントロールスレッドで handler に渡す。
    /// 不正なメッセージは捨て、問題のあったフィールドの名前だけをログに残す（値は残さない）。
    /// @param message 1 メッセージ分の JSON（未検証）
    void on_message(std::string_view message) override;

    /// 待ち受けを続けられなくなった。UI はつながり直せなくなるが、音声とセッションは続ける
    /// （UI の障害でオーディオ処理を止めない）。セッションがなければ自動終了の規則で終わる。
    void on_closed() override;

private:
    ControlLoop& loop_;
    Handler handler_;
    common::Logger& logger_;
};

}  // namespace vocroz::engine
