// SPDX-License-Identifier: AGPL-3.0-only
//
// 子機との接続を持たない PeerLink。子機の受信の組み立て（network と transport、E1 の 2 本目の
// PR）が 入るまでの仮の実装で、子機は 1 台もつながらない。session は PeerHandle
// を受け取ったときにだけ このポートを呼ぶので、ここに呼び出しが来ることはない。2 本目の PR
// で差し替えて、このファイルは消す （docs/adr/0028-engine-assembly.md）。
#pragma once

#include "vocroz/session/ports/peer_link.h"

namespace vocroz::engine {

/// 何もしない PeerLink。
/// スレッド安全性: コントロールスレッドから呼ぶ。
class UnconnectedPeerLink final : public session::PeerLink {
public:
    void send(session::PeerHandle /*peer*/, std::span<const std::uint8_t> /*message*/) override {}
    void admit(session::PeerHandle /*peer*/, const std::string& /*strip_id*/) override {}
    void apply_send_mute(session::PeerHandle /*peer*/, bool /*send_muted*/,
                         std::uint32_t /*epoch*/) override {}
    void close(session::PeerHandle /*peer*/) override {}
};

}  // namespace vocroz::engine
