// SPDX-License-Identifier: AGPL-3.0-only
//
// 子機から親機へ送る MIDI のメッセージ（信頼制御層の MIDI
// レーンで運ぶ本文）のエンコードとデコード。
// 子機は承認前でもメッセージを送れるので、親機はここで形式を検証し、内側には検証済みの
// チャンネルメッセージだけを渡す（信頼境界、AGENTS.md 6.3）。
//
// 形式（docs/adr/0029-midi-and-vsti.md）: 先頭 1 バイトが種別（MIDI イベント）。続いてイベントを
// 1 件以上並べる。各イベントは子機の時計での時刻（マイクロ秒、32 ビット、ビッグエンディアン、
// 桁あふれで一周する）と、チャンネルメッセージ（1〜3 バイト、長さはステータスバイトで決まる）。
// ランニングステータス、SysEx、システムメッセージは受け付けない。不正なイベントを 1 件でも含む
// メッセージは全体を捨てる。一部だけ通すと、Note On だけが届いて Note Off を落とすことがあるため。
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "vocroz/common/result.h"

#include "wire/peer_message_codec.h"

namespace vocroz::session::wire {

/// 子機から届く MIDI イベント 1 件。
struct PeerMidiEvent {
    /// 子機の時計での時刻（マイクロ秒、桁あふれで一周する）。
    std::uint32_t sender_time_us = 0;
    std::array<std::uint8_t, 3> data{};  ///< チャンネルメッセージ
    std::uint8_t size = 0;               ///< data のうち有効なバイト数（1〜3）

    friend bool operator==(const PeerMidiEvent&, const PeerMidiEvent&) = default;
};

/// MIDI のメッセージをエンコードする。
/// @param events イベント（1 件以上、どれも正しいチャンネルメッセージであること）
/// @return MIDI レーンのメッセージの本文
/// スレッド安全性: どのスレッドからも呼び出し可能（状態を持たない）。
[[nodiscard]] std::vector<std::uint8_t> encode_midi(std::span<const PeerMidiEvent> events);

/// 親機が MIDI レーンで受け取ったメッセージをデコードする。
/// @param bytes メッセージの本文
/// @return 検証済みのイベント（1 件以上）。種別が違えば kUnknownType、形式が不正なら kMalformed
/// スレッド安全性: どのスレッドからも呼び出し可能（状態を持たない）。
[[nodiscard]] common::Result<std::vector<PeerMidiEvent>, PeerMessageError> decode_midi(
    std::span<const std::uint8_t> bytes);

}  // namespace vocroz::session::wire
