// SPDX-License-Identifier: AGPL-3.0-only
//
// HostConnection（HostLink の実装）が、親機への接続の結果を知らせるリスナー。
// ClientRuntime が実装し、ClientSession の on_link_*() へ渡す。HostConnection が ClientSession を
// 直接知らないようにして、接続の手順だけを単体テストできるようにする。
#pragma once

#include <cstdint>
#include <span>

#include "vocroz/control_protocol/states.h"

namespace vocroz::client {

/// 親機への接続の結果の受け取り手。
/// スレッド安全性: どれも HostConnection::poll() の中で、コントロールスレッドから呼ばれる。
/// この中から HostLink のメンバー関数を呼んでよい。
class HostLinkListener {
public:
    virtual ~HostLinkListener() = default;

    /// 経路と鍵交換ができた（最初の接続か、再接続）。
    virtual void on_link_established() = 0;

    /// 親機から制御メッセージが届いた。
    /// @param message session の制御メッセージの本文。呼び出しから戻った後は参照しない
    virtual void on_link_message(std::span<const std::uint8_t> message) = 0;

    /// 親機からの受信が途絶えた。
    virtual void on_link_lost() = 0;

    /// 途絶えていた接続から、またパケットが届いた。
    virtual void on_link_resumed() = 0;

    /// 最初の接続で経路を確立できなかった。再接続中の失敗は知らせず、バックオフで試し続ける
    /// （諦める時刻は session の再接続の猶予が決める）。
    /// @param reason 原因
    virtual void on_link_failed(control_protocol::ErrorCode reason) = 0;
};

}  // namespace vocroz::client
