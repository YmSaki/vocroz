// SPDX-License-Identifier: AGPL-3.0-only
//
// 親機から子機との接続を操作するポート。transport
// の信頼制御層と受信の組み立て（network、E1）が実装する。 子機との制御メッセージの形式は session
// が決め（docs/adr/0019-reliable-control-layer.md）、
// このポートはエンコード済みのバイト列を運ぶだけにする。
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "vocroz/session/identifiers.h"
#include "vocroz/session/input_stream.h"

namespace vocroz::session {

/// 親機側の、子機との接続の操作。
/// スレッド安全性: コントロールスレッドから呼ぶ。実装はブロックしない。
class PeerLink {
public:
    virtual ~PeerLink() = default;

    /// 制御メッセージを 1 つ、信頼性ありで送る。
    /// @param peer    送り先の接続
    /// @param message session がエンコードした制御メッセージ
    virtual void send(PeerHandle peer, std::span<const std::uint8_t> message) = 0;

    /// 承認した接続のメディアを受け付け、ストリーム ID ごとにストリップのチャンネルへ流し始める。
    /// 子機の入力の申告が変わるたびに呼び直し、前回の対応を置き換える。対応にないストリームの
    /// メディアと、承認前の接続から届いたメディアは、受信側で捨てる（承認前遮断）。
    /// 実装は、ストリームのチャンネル数で再生（Opus のデコーダー）とミキサーのソースを作る。
    /// @param peer   接続
    /// @param routes ストリームと流し込み先のストリップの対応（1 本以上）
    virtual void admit(PeerHandle peer, const std::vector<StreamRoute>& routes) = 0;

    /// 子機の送信ミュートの通知を、メディアの受信側（信号状態の判定）へ渡す。
    /// @param peer       接続
    /// @param send_muted 通知されたミュート状態
    /// @param epoch      通知に載っていたミュートの世代番号
    virtual void apply_send_mute(PeerHandle peer, bool send_muted, std::uint32_t epoch) = 0;

    /// 接続を閉じる。以降、この接続からのメディアと制御メッセージは捨てる。
    /// @param peer 接続
    virtual void close(PeerHandle peer) = 0;
};

}  // namespace vocroz::session
