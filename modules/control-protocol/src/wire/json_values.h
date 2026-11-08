// SPDX-License-Identifier: AGPL-3.0-only
//
// C++ の値を JSON の値にする小さな変換。null で表す値（-∞ dB、未設定）の扱いを 1 か所にまとめる。
#pragma once

#include <cmath>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "wire/enum_names.h"

namespace vocroz::control_protocol::wire {

/// std::nullopt を null にする。
[[nodiscard]] inline nlohmann::json nullable(const std::optional<std::string>& value) {
    return value ? nlohmann::json(*value) : nlohmann::json(nullptr);
}

/// 数値を JSON にする。std::nullopt（-∞ dB、無音）と、JSON で表せない NaN と無限大は null にする。
[[nodiscard]] inline nlohmann::json nullable(const std::optional<double>& value) {
    return value && std::isfinite(*value) ? nlohmann::json(*value) : nlohmann::json(nullptr);
}

/// 列挙子を正規名の文字列にする。
template <typename E>
[[nodiscard]] nlohmann::json enum_json(E value) {
    return nlohmann::json(name_of(value));
}

/// 列挙子を正規名の文字列にし、std::nullopt は null にする。
template <typename E>
[[nodiscard]] nlohmann::json enum_json(const std::optional<E>& value) {
    return value ? enum_json(*value) : nlohmann::json(nullptr);
}

}  // namespace vocroz::control_protocol::wire
