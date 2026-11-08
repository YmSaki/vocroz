// SPDX-License-Identifier: AGPL-3.0-only
//
// json_codec.h の実装。大きさの検査と JSON の構文解析をここで行い、メッセージの中身は wire/
// に任せる。

#include "vocroz/control_protocol/json_codec.h"

#include <nlohmann/json.hpp>

#include "vocroz/control_protocol/constants.h"

#include "wire/command_codec.h"
#include "wire/event_codec.h"

namespace vocroz::control_protocol {
namespace {

/// 大きさを確かめてから JSON として読む。例外は使わない（docs/adr/0010-external-libraries.md）。
common::Result<nlohmann::json, DecodeError> parse(std::string_view text) {
    if (text.size() > kMaxMessageBytes) {
        return common::make_unexpected(DecodeError{DecodeErrorCode::kTooLarge, {}});
    }
    // 第 3 引数 false で、構文の誤り（UTF-8
    // として不正な文字列を含む）を例外ではなく破棄された値で返させる。
    auto value = nlohmann::json::parse(text, nullptr, false);
    if (value.is_discarded()) {
        return common::make_unexpected(DecodeError{DecodeErrorCode::kMalformedJson, {}});
    }
    return value;
}

/// JSON を文字列にする。ネットワーク由来の文字列が UTF-8 として不正でも例外を出さず、U+FFFD
/// に置き換える。
std::string dump(const nlohmann::json& value) {
    return value.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

}  // namespace

common::Result<UiToEngineMessage, DecodeError> decode_ui_to_engine(std::string_view text) {
    auto value = parse(text);
    if (!value) {
        return common::make_unexpected(value.error());
    }
    return wire::read_ui_to_engine(value.value());
}

std::string encode_ui_to_engine(const UiToEngineMessage& message) {
    return dump(wire::write_ui_to_engine(message));
}

common::Result<EngineToUiMessage, DecodeError> decode_engine_to_ui(std::string_view text) {
    auto value = parse(text);
    if (!value) {
        return common::make_unexpected(value.error());
    }
    return wire::read_engine_to_ui(value.value());
}

std::string encode_engine_to_ui(const EngineToUiMessage& message) {
    return dump(wire::write_engine_to_ui(message));
}

}  // namespace vocroz::control_protocol
