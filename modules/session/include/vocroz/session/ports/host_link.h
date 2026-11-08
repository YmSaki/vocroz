// SPDX-License-Identifier: AGPL-3.0-only
//
// 子機から親機への接続を操作するポート。network（ホールパンチ）と transport（Noise_IK、信頼制御層、
// 再接続のバックオフ）の組み立てが実装する。結果は ClientSession の on_link_*() で返す。
#pragma once

#include <cstdint>
#include <span>

#include "vocroz/session/identifiers.h"

namespace vocroz::session {

/// 招待を解決して得た、親機への接続に必要な情報。
struct HostEndpoint {
    /// 親機の静的公開鍵。Noise_IK の相手の鍵として使い、親機のなりすましを防ぐ。
    HostPublicKey host_public_key{};
    /// ホールパンチの相手を見つけるためのランデブー ID。
    RendezvousId rendezvous_id{};

    friend bool operator==(const HostEndpoint&, const HostEndpoint&) = default;
};

/// 子機側の、親機への接続の操作。
/// スレッド安全性: コントロールスレッドから呼ぶ。実装はブロックしない。
class HostLink {
public:
    virtual ~HostLink() = default;

    /// 親機への経路の確立（直結かホールパンチ）と鍵交換を始める。
    /// 成功したら ClientSession::on_link_established()、経路を確立できなければ
    /// ClientSession::on_link_failed() で知らせる。
    /// @param endpoint 接続先
    virtual void connect(const HostEndpoint& endpoint) = 0;

    /// 回線の途絶後の再接続を始める。バックオフ（1、2、4、8、15 秒）は実装が持つ。
    /// 再接続できたら ClientSession::on_link_established() で知らせる。
    virtual void reconnect() = 0;

    /// 制御メッセージを 1 つ、信頼性ありで送る。
    /// @param message session がエンコードした制御メッセージ
    virtual void send(std::span<const std::uint8_t> message) = 0;

    /// 接続を閉じ、再接続の試行もやめる。
    virtual void close() = 0;
};

}  // namespace vocroz::session
