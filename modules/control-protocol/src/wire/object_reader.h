// SPDX-License-Identifier: AGPL-3.0-only
//
// JSON のオブジェクトからフィールドを検証しながら読み出すヘルパー。
// 信頼境界の検査（フィールドの有無と型、値の範囲、未知のフィールド）をここに集約し、
// メッセージごとのデコーダーは「どのフィールドをどの規則で読むか」だけを書けばよいようにする。
// 例外は使わず、最初に見つかった問題を記録して以降の読み出しを無視する。
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "vocroz/common/result.h"
#include "vocroz/control_protocol/entities.h"
#include "vocroz/control_protocol/json_codec.h"

#include "wire/enum_names.h"

namespace vocroz::control_protocol::wire {

/// 文字列に使ってよい文字の集合。
enum class Charset {
    kAny,         ///< 制限なし（表示用の文字列）
    kIdentifier,  ///< 英数字と _ - . :（UI とエンジンが振る ID）
    kLowerSnake,  ///< 英小文字、数字、_（メッセージの種別、通知の種別）
};

/// 文字列のフィールドの規則。
struct TextRule {
    std::size_t min_bytes;
    std::size_t max_bytes;
    Charset charset;
};

/// 文字列が規則を満たすかを返す。
/// @param text  確かめる文字列（UTF-8 として正しいことはパーサーが確かめている）
/// @param rule  規則
[[nodiscard]] bool satisfies(std::string_view text, const TextRule& rule) noexcept;

/// 1 つの JSON オブジェクトのフィールドを読み出す。
/// 読み出し関数は問題があれば記録して既定値を返し、以降は何もしない。最後に finish()
/// で結果を確かめる。
class ObjectReader {
public:
    /// @param object 読み出すオブジェクト。is_object() であること。読み出しの間は生存させる
    explicit ObjectReader(const nlohmann::json& object);

    /// 真偽値を読む。
    [[nodiscard]] bool boolean(std::string_view key);

    /// revision かシーケンス番号を読む。0 以上 kMaxCounter 以下の整数だけを受け付ける。
    [[nodiscard]] std::uint64_t counter(std::string_view key);

    /// ゲインを読む。null（-∞ dB）か、kMinFiniteGainDb 以上 kMaxGainDb
    /// 以下の有限の数値だけを受け付ける。
    [[nodiscard]] GainDb gain(std::string_view key);

    /// min 以上 max 以下の数値を読む。
    [[nodiscard]] double number(std::string_view key, double min, double max);

    /// null か有限の数値を読む。
    [[nodiscard]] std::optional<double> nullable_number(std::string_view key);

    /// 規則を満たす文字列を読む。
    [[nodiscard]] std::string text(std::string_view key, const TextRule& rule);

    /// null か、規則を満たす文字列を読む。
    [[nodiscard]] std::optional<std::string> nullable_text(std::string_view key,
                                                           const TextRule& rule);

    /// 正規名の文字列を列挙子として読む。
    template <typename E>
    [[nodiscard]] E enumeration(std::string_view key) {
        const nlohmann::json* value = find(key);
        if (value == nullptr) {
            return E{};
        }
        return to_enum<E>(*value, key).value_or(E{});
    }

    /// null か、正規名の文字列を列挙子として読む。
    template <typename E>
    [[nodiscard]] std::optional<E> nullable_enumeration(std::string_view key) {
        const nlohmann::json* value = find(key);
        if (value == nullptr || value->is_null()) {
            return std::nullopt;
        }
        return to_enum<E>(*value, key);
    }

    /// 入れ子のオブジェクトを read で読む。
    /// @param read `common::Result<T, DecodeError>(const nlohmann::json&)` の関数
    template <typename T, typename Read>
    [[nodiscard]] T nested(std::string_view key, Read read) {
        const nlohmann::json* value = find(key);
        if (value == nullptr) {
            return T{};
        }
        auto result = read(*value);
        if (!result) {
            fail_nested(key, result.error());
            return T{};
        }
        return std::move(result).value();
    }

    /// null か、入れ子のオブジェクトを read で読む。
    /// @param read `common::Result<T, DecodeError>(const nlohmann::json&)` の関数
    template <typename T, typename Read>
    [[nodiscard]] std::optional<T> nullable_nested(std::string_view key, Read read) {
        const nlohmann::json* value = find(key);
        if (value == nullptr || value->is_null()) {
            return std::nullopt;
        }
        auto result = read(*value);
        if (!result) {
            fail_nested(key, result.error());
            return std::nullopt;
        }
        return std::move(result).value();
    }

    /// 配列の各要素を read で読む。
    /// @param read `common::Result<T, DecodeError>(const nlohmann::json&)` の関数
    template <typename T, typename Read>
    [[nodiscard]] std::vector<T> list(std::string_view key, Read read) {
        const nlohmann::json* value = find(key);
        if (value == nullptr) {
            return {};
        }
        if (!value->is_array()) {
            fail(DecodeErrorCode::kWrongType, key);
            return {};
        }
        std::vector<T> items;
        items.reserve(value->size());
        for (const auto& element : *value) {
            auto result = read(element);
            if (!result) {
                fail_nested(key, result.error());
                return {};
            }
            items.push_back(std::move(result).value());
        }
        return items;
    }

    /// 入れ子のオブジェクトをそのまま返す。中身の検査は呼び出し側が行う。
    /// @return オブジェクト。なければ nullptr（問題は記録済み）
    [[nodiscard]] const nlohmann::json* object(std::string_view key);

    /// これまでの読み出しに問題がなければ true を返す。
    [[nodiscard]] bool ok() const noexcept {
        return !error_.has_value();
    }

    /// 問題を記録する。すでに記録があれば何もしない。
    void fail(DecodeErrorCode code, std::string_view field);

    /// 読み出しを終える。読み出さなかったフィールドが残っていれば kUnknownField を記録する。
    /// @return 問題がなければ成功、あれば最初の問題
    [[nodiscard]] common::Result<void, DecodeError> finish();

private:
    /// フィールドを探して既読にする。なければ kMissingField を記録して nullptr を返す。
    [[nodiscard]] const nlohmann::json* find(std::string_view key);

    /// 入れ子の問題を、フィールド名に親のキーを前置して記録する。
    void fail_nested(std::string_view key, const DecodeError& inner);

    template <typename E>
    [[nodiscard]] std::optional<E> to_enum(const nlohmann::json& value, std::string_view key) {
        if (!value.is_string()) {
            fail(DecodeErrorCode::kWrongType, key);
            return std::nullopt;
        }
        const auto result = value_of<E>(value.get_ref<const std::string&>());
        if (!result) {
            fail(DecodeErrorCode::kOutOfRange, key);
        }
        return result;
    }

    const nlohmann::json& object_;
    /// 読み出したフィールドの名前。finish() で未知のフィールドを見つけるために使う。
    std::vector<std::string_view> read_keys_;
    std::optional<DecodeError> error_;
};

/// value がオブジェクトなら reader を作って read_fields(reader) を呼び、finish()
/// の結果とともに返す。
/// @param read_fields `T(ObjectReader&)` の関数
template <typename T, typename ReadFields>
[[nodiscard]] common::Result<T, DecodeError> read_object(const nlohmann::json& value,
                                                         ReadFields read_fields) {
    if (!value.is_object()) {
        return common::make_unexpected(DecodeError{DecodeErrorCode::kNotObject, {}});
    }
    ObjectReader reader{value};
    T result = read_fields(reader);
    if (auto finished = reader.finish(); !finished) {
        return common::make_unexpected(finished.error());
    }
    return result;
}

}  // namespace vocroz::control_protocol::wire
