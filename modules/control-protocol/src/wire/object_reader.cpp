// SPDX-License-Identifier: AGPL-3.0-only
//
// ObjectReader の実装。

#include "wire/object_reader.h"

#include <algorithm>
#include <cmath>

#include "vocroz/control_protocol/constants.h"

namespace vocroz::control_protocol::wire {
namespace {

bool is_identifier_char(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
           c == '-' || c == '.' || c == ':';
}

bool is_lower_snake_char(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

}  // namespace

bool satisfies(std::string_view text, const TextRule& rule) noexcept {
    if (text.size() < rule.min_bytes || text.size() > rule.max_bytes) {
        return false;
    }
    switch (rule.charset) {
        case Charset::kAny:
            return true;
        case Charset::kIdentifier:
            return std::all_of(text.begin(), text.end(), is_identifier_char);
        case Charset::kLowerSnake:
            return std::all_of(text.begin(), text.end(), is_lower_snake_char);
    }
    return false;
}

ObjectReader::ObjectReader(const nlohmann::json& object) : object_(object) {}

bool ObjectReader::boolean(std::string_view key) {
    const nlohmann::json* value = find(key);
    if (value == nullptr) {
        return false;
    }
    if (!value->is_boolean()) {
        fail(DecodeErrorCode::kWrongType, key);
        return false;
    }
    return value->get<bool>();
}

std::uint64_t ObjectReader::counter(std::string_view key) {
    const nlohmann::json* value = find(key);
    if (value == nullptr) {
        return 0;
    }
    if (value->is_number_unsigned()) {
        const auto number = value->get<std::uint64_t>();
        if (number > kMaxCounter) {
            fail(DecodeErrorCode::kOutOfRange, key);
            return 0;
        }
        return number;
    }
    // nlohmann/json は負の整数を number_integer、小数を number_float として読む。
    fail(value->is_number_integer() ? DecodeErrorCode::kOutOfRange : DecodeErrorCode::kWrongType,
         key);
    return 0;
}

GainDb ObjectReader::gain(std::string_view key) {
    const nlohmann::json* value = find(key);
    if (value == nullptr || value->is_null()) {
        return std::nullopt;
    }
    if (!value->is_number()) {
        fail(DecodeErrorCode::kWrongType, key);
        return std::nullopt;
    }
    const auto db = value->get<double>();
    if (!std::isfinite(db) || db < kMinFiniteGainDb || db > kMaxGainDb) {
        fail(DecodeErrorCode::kOutOfRange, key);
        return std::nullopt;
    }
    return db;
}

double ObjectReader::number(std::string_view key, double min, double max) {
    const nlohmann::json* value = find(key);
    if (value == nullptr) {
        return min;
    }
    if (!value->is_number()) {
        fail(DecodeErrorCode::kWrongType, key);
        return min;
    }
    const auto number = value->get<double>();
    if (!std::isfinite(number) || number < min || number > max) {
        fail(DecodeErrorCode::kOutOfRange, key);
        return min;
    }
    return number;
}

std::optional<double> ObjectReader::nullable_number(std::string_view key) {
    const nlohmann::json* value = find(key);
    if (value == nullptr || value->is_null()) {
        return std::nullopt;
    }
    if (!value->is_number()) {
        fail(DecodeErrorCode::kWrongType, key);
        return std::nullopt;
    }
    const auto number = value->get<double>();
    if (!std::isfinite(number)) {
        fail(DecodeErrorCode::kOutOfRange, key);
        return std::nullopt;
    }
    return number;
}

std::string ObjectReader::text(std::string_view key, const TextRule& rule) {
    const nlohmann::json* value = find(key);
    if (value == nullptr) {
        return {};
    }
    if (!value->is_string()) {
        fail(DecodeErrorCode::kWrongType, key);
        return {};
    }
    const auto& text = value->get_ref<const std::string&>();
    if (!satisfies(text, rule)) {
        fail(DecodeErrorCode::kOutOfRange, key);
        return {};
    }
    return text;
}

std::optional<std::string> ObjectReader::nullable_text(std::string_view key, const TextRule& rule) {
    const nlohmann::json* value = find(key);
    if (value == nullptr || value->is_null()) {
        return std::nullopt;
    }
    if (!value->is_string()) {
        fail(DecodeErrorCode::kWrongType, key);
        return std::nullopt;
    }
    const auto& text = value->get_ref<const std::string&>();
    if (!satisfies(text, rule)) {
        fail(DecodeErrorCode::kOutOfRange, key);
        return std::nullopt;
    }
    return text;
}

const nlohmann::json* ObjectReader::object(std::string_view key) {
    const nlohmann::json* value = find(key);
    if (value == nullptr) {
        return nullptr;
    }
    if (!value->is_object()) {
        fail(DecodeErrorCode::kWrongType, key);
        return nullptr;
    }
    return value;
}

void ObjectReader::fail(DecodeErrorCode code, std::string_view field) {
    if (!error_) {
        error_ = DecodeError{code, std::string{field}};
    }
}

common::Result<void, DecodeError> ObjectReader::finish() {
    if (!error_ && read_keys_.size() != object_.size()) {
        // 同じキーを 2 回読むことはないので、数が合わなければ読んでいないキーがある。
        for (const auto& [key, value] : object_.items()) {
            if (std::find(read_keys_.begin(), read_keys_.end(), key) == read_keys_.end()) {
                fail(DecodeErrorCode::kUnknownField, key);
                break;
            }
        }
    }
    if (error_) {
        return common::make_unexpected(*error_);
    }
    return {};
}

const nlohmann::json* ObjectReader::find(std::string_view key) {
    if (error_) {
        return nullptr;
    }
    const auto it = object_.find(key);
    if (it == object_.end()) {
        fail(DecodeErrorCode::kMissingField, key);
        return nullptr;
    }
    read_keys_.push_back(key);
    return &*it;
}

void ObjectReader::fail_nested(std::string_view key, const DecodeError& inner) {
    std::string field{key};
    if (!inner.field.empty()) {
        field += '.';
        field += inner.field;
    }
    fail(inner.code, field);
}

}  // namespace vocroz::control_protocol::wire
