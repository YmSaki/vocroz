// SPDX-License-Identifier: AGPL-3.0-only
//
// control-protocol の中だけで使う、文字列のフィールドの長さの上限。
// どれも想定される値より十分大きくし、異常に長い値だけを拒否する。
#pragma once

#include <cstddef>

namespace vocroz::control_protocol {

/// request_id、entity_id、participant_id の最大バイト数。UI とエンジンが振る短い ID なので 64
/// で足りる。
inline constexpr std::size_t kMaxIdentifierBytes = 64;

/// 招待コードか招待 URL の最大バイト数。URL の一般的な実用上限に合わせる。
inline constexpr std::size_t kMaxInviteBytes = 2048;

/// 招待 URL（invite_url）の最大バイト数。kMaxInviteBytes と同じ値の URL を表示するため。
inline constexpr std::size_t kMaxUrlBytes = kMaxInviteBytes;

/// 入力デバイスの永続 ID の最大バイト数。WASAPI のエンドポイント ID は 60 字前後で、余裕を見る。
inline constexpr std::size_t kMaxDeviceIdBytes = 512;

/// 参加者名、セッション名、デバイス名、短い識別 ID、バージョンなどの表示用の文字列の最大バイト数。
inline constexpr std::size_t kMaxTextBytes = 512;

/// 通知の種別（notice の code）と、埋め込む値の名前の最大バイト数。
inline constexpr std::size_t kMaxNoticeKeyBytes = 64;

/// 通知に埋め込む値の最大バイト数。
inline constexpr std::size_t kMaxNoticeValueBytes = 1024;

}  // namespace vocroz::control_protocol
