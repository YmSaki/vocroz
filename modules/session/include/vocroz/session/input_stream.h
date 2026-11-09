// SPDX-License-Identifier: AGPL-3.0-only
//
// 子機の入力（ストリーム）と、親機でそれを流し込むストリップの対応の値型。
// 1 台の子機はマイクやアプリ音声などの入力を複数送れ、入力ごとに親機の別のストリップになる
// （docs/adr/0035-client-input-expansion.md）。
// 子機は入力の一覧を制御メッセージで申告し、メディアパケットのストリーム ID で入力を区別する。
#pragma once

#include <cstdint>
#include <string>

#include "vocroz/control_protocol/states.h"

namespace vocroz::session {

/// 子機の入力 1 つの申告。
struct InputStream {
    /// メディアパケットのストリーム ID。子機が接続の中で一意に振る。
    std::uint16_t stream_id = 0;
    /// 入力の種類。ストリップの表示に使う。
    control_protocol::InputSourceKind kind = control_protocol::InputSourceKind::kMicrophone;
    /// チャンネル数（1 か 2）。Opus のデコーダーとストリップのチャンネル数になる。
    std::uint8_t channels = 1;

    friend bool operator==(const InputStream&, const InputStream&) = default;
};

/// 承認した接続のストリーム 1 本を流し込む先。
struct StreamRoute {
    /// メディアパケットのストリーム ID。
    std::uint16_t stream_id = 0;
    /// チャンネル数（1 か 2）。
    std::uint8_t channels = 1;
    /// 流し込むチャンネルストリップの entity_id。
    std::string strip_id;

    friend bool operator==(const StreamRoute&, const StreamRoute&) = default;
};

}  // namespace vocroz::session
