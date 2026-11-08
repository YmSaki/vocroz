// SPDX-License-Identifier: AGPL-3.0-only
//
// エンジンから UI へのメッセージ（events.h）と JSON の変換。
#pragma once

#include <nlohmann/json.hpp>

#include "vocroz/common/result.h"
#include "vocroz/control_protocol/events.h"
#include "vocroz/control_protocol/json_codec.h"

namespace vocroz::control_protocol::wire {

/// メッセージを JSON のオブジェクトにする。
[[nodiscard]] nlohmann::json write_engine_to_ui(const EngineToUiMessage& message);

/// JSON の値をメッセージとして読む。
/// @param value パース済みの JSON（オブジェクトでなければ kNotObject）
[[nodiscard]] common::Result<EngineToUiMessage, DecodeError> read_engine_to_ui(
    const nlohmann::json& value);

}  // namespace vocroz::control_protocol::wire
