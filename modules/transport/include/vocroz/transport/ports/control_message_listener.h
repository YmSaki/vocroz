// SPDX-License-Identifier: AGPL-3.0-only
//
// 信頼制御層が受け取った制御メッセージを外側（session など）に渡すリスナー（AGENTS.md 4.1）。
// メッセージの中身の形式は外側が決め、transport は不透明なバイト列として運ぶ。
#pragma once

#include <cstdint>
#include <span>

#include "vocroz/transport/control_lane.h"

namespace vocroz::transport {

/// 制御メッセージの受け取り手。
class ControlMessageListener {
public:
    virtual ~ControlMessageListener() = default;

    /// 相手が送った制御メッセージを、レーンごとに送信順で 1 回ずつ受け取る。
    /// 重複と順序の入れ替わりは信頼制御層が取り除いた後なので、受け取り手は気にしなくてよい。
    /// @param lane    メッセージのレーン
    /// @param message メッセージの中身。呼び出しから戻った後は参照しない
    /// スレッド安全性: ReliableControlChannel::on_control_packet() を呼んだスレッドから、
    /// その呼び出しの中で呼ばれる。この中から ReliableControlChannel::send() を呼んでよい。
    virtual void on_control_message(ControlLane lane, std::span<const std::uint8_t> message) = 0;
};

}  // namespace vocroz::transport
