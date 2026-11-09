// SPDX-License-Identifier: AGPL-3.0-only
//
// 受信側を持たない PlayoutControl。子機の受信の組み立て（E1 の子機の受信の PR）が入るまでの
// 仮の実装で、子機の再生ストリームがまだないので目標レイテンシーを配る先がない。
// session は目標を設定とセッションの状態に記録するので、受信を組んだときにはその値で作れる。
// 受信の PR で差し替えて、このファイルは消す（docs/adr/0028-engine-assembly.md）。
#pragma once

#include "vocroz/session/ports/playout_control.h"

namespace vocroz::engine {

/// 何もしない PlayoutControl。
/// スレッド安全性: コントロールスレッドから呼ぶ。
class UnconnectedPlayoutControl final : public session::PlayoutControl {
public:
    void set_latency_target(std::chrono::microseconds /*target*/) override {}
};

}  // namespace vocroz::engine
