// SPDX-License-Identifier: AGPL-3.0-only
//
// UI からエンジンへのメッセージ（commands.h）と JSON の変換。
#pragma once

#include <nlohmann/json.hpp>

#include "vocroz/common/result.h"
#include "vocroz/control_protocol/commands.h"
#include "vocroz/control_protocol/json_codec.h"

namespace vocroz::control_protocol::wire {

/// メッセージを JSON のオブジェクトにする。
[[nodiscard]] nlohmann::json write_ui_to_engine(const UiToEngineMessage& message);

/// JSON の値をメッセージとして読む。
/// @param value パース済みの JSON（オブジェクトでなければ kNotObject）
[[nodiscard]] common::Result<UiToEngineMessage, DecodeError> read_ui_to_engine(
    const nlohmann::json& value);

}  // namespace vocroz::control_protocol::wire
