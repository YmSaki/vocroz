// SPDX-License-Identifier: AGPL-3.0-only
//
// UI とエンジン間のメッセージの JSON のエンコードとデコード。
// デコードは信頼境界で、JSON の構文、種別、フィールドの有無と型、未知のフィールド、
// どの entity にも共通する値の範囲を検査し、検証済みの型だけを返す
// （docs/adr/0015-control-protocol-messages.md）。entity ごとの範囲と状態との整合は session
// が判断する。
#pragma once

#include <string>
#include <string_view>

#include "vocroz/common/result.h"
#include "vocroz/control_protocol/commands.h"
#include "vocroz/control_protocol/events.h"

namespace vocroz::control_protocol {

/// デコードに失敗した原因。
enum class DecodeErrorCode {
    kTooLarge,       ///< kMaxMessageBytes を超えている
    kMalformedJson,  ///< JSON として読めない（UTF-8 として不正な文字列を含む）
    kNotObject,      ///< メッセージかその一部がオブジェクトでない
    kUnknownType,    ///< type が未知の種別
    kMissingField,   ///< 必須のフィールドがない
    kWrongType,      ///< フィールドの型が違う
    kOutOfRange,     ///< 値が範囲外か、許されない文字を含む
    kUnknownField,   ///< 定義されていないフィールドがある
};

/// デコードの失敗。
struct DecodeError {
    DecodeErrorCode code = DecodeErrorCode::kMalformedJson;
    /// 問題のあったフィールドの名前。メッセージ全体の問題なら空。ログに出してよい（値は含まない）。
    std::string field;

    friend bool operator==(const DecodeError&, const DecodeError&) = default;
};

/// UI からエンジンへのメッセージをデコードする。エンジン側の信頼境界で使う。
/// @param text 1 メッセージ分の JSON
/// @return 検証済みのメッセージ。不正なら原因
/// スレッド安全性: 状態を持たないので、どのスレッドからも同時に呼び出し可能。
/// ヒープを確保するので、オーディオスレッドからは呼び出さない。
[[nodiscard]] common::Result<UiToEngineMessage, DecodeError> decode_ui_to_engine(
    std::string_view text);

/// UI からエンジンへのメッセージを JSON にエンコードする。
/// @param message エンコードするメッセージ
/// @return 1 メッセージ分の JSON。UTF-8 として不正な文字列は U+FFFD に置き換える
/// スレッド安全性: decode_ui_to_engine() と同じ。
[[nodiscard]] std::string encode_ui_to_engine(const UiToEngineMessage& message);

/// エンジンから UI へのメッセージをデコードする。検査の規則は decode_ui_to_engine() と同じ。
/// @param text 1 メッセージ分の JSON
/// @return 検証済みのメッセージ。不正なら原因
/// スレッド安全性: decode_ui_to_engine() と同じ。
[[nodiscard]] common::Result<EngineToUiMessage, DecodeError> decode_engine_to_ui(
    std::string_view text);

/// エンジンから UI へのメッセージを JSON にエンコードする。
/// @param message エンコードするメッセージ
/// @return 1 メッセージ分の JSON。UTF-8 として不正な文字列（ネットワーク由来の参加者名など）は
/// U+FFFD に置き換える。-∞ dB と無音は null になる スレッド安全性: decode_ui_to_engine() と同じ。
[[nodiscard]] std::string encode_engine_to_ui(const EngineToUiMessage& message);

}  // namespace vocroz::control_protocol
