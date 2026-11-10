// SPDX-License-Identifier: AGPL-3.0-only
//
// 文字列のフィールドの種類ごとの規則。同じ種類のフィールドは、どのメッセージでも同じ規則で検査する。
#pragma once

#include "vocroz/control_protocol/constants.h"

#include "control_protocol_constants.h"
#include "wire/object_reader.h"

namespace vocroz::control_protocol::wire {

/// request_id、entity_id、participant_id。
inline constexpr TextRule kIdentifierRule{1, kMaxIdentifierBytes, Charset::kIdentifier};

/// メッセージの種別（type）。
inline constexpr TextRule kTypeRule{1, kMaxIdentifierBytes, Charset::kLowerSnake};

/// 招待コードか招待 URL。形式の検証は session が行う。
inline constexpr TextRule kInviteRule{1, kMaxInviteBytes, Charset::kAny};

/// 招待 URL（表示用）。
inline constexpr TextRule kUrlRule{1, kMaxUrlBytes, Charset::kAny};

/// 入力デバイスの永続 ID。
inline constexpr TextRule kDeviceIdRule{1, kMaxDeviceIdBytes, Charset::kAny};

/// プラグインの識別子（JUCE の PluginDescription::createIdentifierString()）。名前を含むので
/// 文字の種類は制限しない。
inline constexpr TextRule kPluginIdRule{1, kMaxPluginIdBytes, Charset::kAny};

/// 表示用の文字列（名前、短い識別 ID、バージョン）。IDLE のセッション名のように空のこともある。
inline constexpr TextRule kTextRule{0, kMaxTextBytes, Charset::kAny};

/// 通知の種別と、埋め込む値の名前。UI の文字列リソースのキーと埋め込み位置の名前になる。
inline constexpr TextRule kNoticeKeyRule{1, kMaxNoticeKeyBytes, Charset::kLowerSnake};

/// 通知に埋め込む値。
inline constexpr TextRule kNoticeValueRule{0, kMaxNoticeValueBytes, Charset::kAny};

/// SRTP 外部出力の宛先のホスト名か IP アドレス。未設定は空。形式の検証は名前解決で行う。
inline constexpr TextRule kHostRule{0, kMaxExternalOutputHostBytes, Charset::kAny};
/// SRTP 外部出力の鍵。形式の検証は session が行う（transport の parse_srtp_master_key()）。
inline constexpr TextRule kSrtpKeyRule{1, kMaxSrtpKeyBytes, Charset::kAny};
}  // namespace vocroz::control_protocol::wire
