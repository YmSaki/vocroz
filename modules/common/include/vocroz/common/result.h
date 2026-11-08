// SPDX-License-Identifier: AGPL-3.0-only
//
// 失敗しうる処理の戻り値に使う Result 型。
// モジュール境界の外へ例外を送出しない規則（AGENTS.md 6.2）を守りつつ、
// 失敗の原因をエラーコードとして呼び出し側へ返すために使う。
// C++23 の std::expected と同じ形の API にしてあり、標準を C++23
// に上げたときに置き換えやすくしている。
#pragma once

#include <cassert>
#include <type_traits>
#include <utility>
#include <variant>

namespace vocroz::common {

/// Result にエラーを格納するためのラッパー。make_unexpected() で作る。
/// 値の型とエラーの型が同じでも、どちらを返したのかを型で区別できるようにするために使う。
/// @tparam E エラーの型。通常は各モジュールが定義する原因別の enum class
template <typename E>
class Unexpected {
public:
    /// エラー値を保持する。
    /// @param error 保持するエラー
    constexpr explicit Unexpected(E error) noexcept(std::is_nothrow_move_constructible_v<E>)
        : error_(std::move(error)) {}

    /// 保持しているエラーを返す。
    [[nodiscard]] constexpr const E& error() const& noexcept {
        return error_;
    }

    /// 保持しているエラーをムーブして返す。
    [[nodiscard]] constexpr E&& error() && noexcept {
        return std::move(error_);
    }

private:
    E error_;
};

/// エラーを Result に変換できる形で包む。
/// 例: `return make_unexpected(InviteError::kExpired);`
/// @param error 返すエラー
/// @return error を保持する Unexpected
template <typename E>
[[nodiscard]] constexpr Unexpected<std::decay_t<E>> make_unexpected(E&& error) {
    return Unexpected<std::decay_t<E>>(std::forward<E>(error));
}

/// 成功時の値 T か、失敗時のエラー E のどちらか一方を保持する。
/// ヒープを確保しないので、値とエラーの型がヒープを確保しなければオーディオスレッドからも使える。
/// スレッド安全性: 通常の値型と同じ（1 つのオブジェクトを複数スレッドから同時に変更しない）。
/// @tparam T 成功時の値の型
/// @tparam E エラーの型。通常は各モジュールが定義する原因別の enum class
template <typename T, typename E>
class [[nodiscard]] Result {
public:
    static_assert(!std::is_void_v<T>, "値を返さない処理には Result<void, E> を使う");

    /// 成功の Result を作る。
    /// @param value 成功時の値
    /// `return value;` で返せるよう、暗黙の変換を許す。
    // NOLINTNEXTLINE(google-explicit-constructor)
    constexpr Result(T value) noexcept(std::is_nothrow_move_constructible_v<T>)
        : storage_(std::in_place_index<0>, std::move(value)) {}

    /// 失敗の Result を作る。
    /// @param unexpected make_unexpected() で包んだエラー
    /// `return make_unexpected(e);` で返せるよう、暗黙の変換を許す。
    // NOLINTNEXTLINE(google-explicit-constructor)
    constexpr Result(Unexpected<E> unexpected) noexcept(std::is_nothrow_move_constructible_v<E>)
        : storage_(std::in_place_index<1>, std::move(unexpected).error()) {}

    /// 成功していれば true を返す。
    [[nodiscard]] constexpr bool has_value() const noexcept {
        return storage_.index() == 0;
    }

    /// has_value() と同じ。`if (result)` と書けるようにする。
    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return has_value();
    }

    /// 成功時の値を返す。
    /// 前提条件: has_value() が true。失敗の Result で呼ぶのはプログラムの誤りで、
    /// デバッグビルドでは assert で止まる。例外は送出しない。
    [[nodiscard]] constexpr const T& value() const& noexcept {
        assert(has_value());
        return *std::get_if<0>(&storage_);
    }

    /// 成功時の値を変更可能な参照で返す。前提条件は const 版と同じ。
    [[nodiscard]] constexpr T& value() & noexcept {
        assert(has_value());
        return *std::get_if<0>(&storage_);
    }

    /// 成功時の値をムーブして返す。前提条件は const 版と同じ。
    [[nodiscard]] constexpr T&& value() && noexcept {
        assert(has_value());
        return std::move(*std::get_if<0>(&storage_));
    }

    /// 失敗時のエラーを返す。
    /// 前提条件: has_value() が false。成功の Result で呼ぶのはプログラムの誤りで、
    /// デバッグビルドでは assert で止まる。例外は送出しない。
    [[nodiscard]] constexpr const E& error() const noexcept {
        assert(!has_value());
        return *std::get_if<1>(&storage_);
    }

    /// 成功していれば値を、失敗していれば fallback を返す。
    /// @param fallback 失敗時に返す値
    template <typename U>
    [[nodiscard]] constexpr T value_or(U&& fallback) const& {
        return has_value() ? value() : static_cast<T>(std::forward<U>(fallback));
    }

private:
    // 添字 0 が値、1 がエラー。T と E が同じ型でも区別できるよう、型ではなく添字で扱う。
    std::variant<T, E> storage_;
};

/// 値を返さない処理の Result。成功か、失敗時のエラー E を保持する。
/// @tparam E エラーの型
template <typename E>
class [[nodiscard]] Result<void, E> {
public:
    /// 成功の Result を作る。`return {};` で返せる。
    constexpr Result() noexcept = default;

    /// 失敗の Result を作る。
    /// @param unexpected make_unexpected() で包んだエラー
    /// `return make_unexpected(e);` で返せるよう、暗黙の変換を許す。
    // NOLINTNEXTLINE(google-explicit-constructor)
    constexpr Result(Unexpected<E> unexpected) noexcept(std::is_nothrow_move_constructible_v<E>)
        : storage_(std::in_place_index<1>, std::move(unexpected).error()) {}

    /// 成功していれば true を返す。
    [[nodiscard]] constexpr bool has_value() const noexcept {
        return storage_.index() == 0;
    }

    /// has_value() と同じ。`if (result)` と書けるようにする。
    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return has_value();
    }

    /// 失敗時のエラーを返す。
    /// 前提条件: has_value() が false。デバッグビルドでは assert で止まる。例外は送出しない。
    [[nodiscard]] constexpr const E& error() const noexcept {
        assert(!has_value());
        return *std::get_if<1>(&storage_);
    }

private:
    // 添字 0（std::monostate）が成功、1 がエラー。
    std::variant<std::monostate, E> storage_;
};

}  // namespace vocroz::common
