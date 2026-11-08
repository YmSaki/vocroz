// SPDX-License-Identifier: AGPL-3.0-only
//
// ワイヤーフォーマットのパーサーとシリアライザー（src/wire/）が共通で使う、
// ビッグエンディアンのバイト列の読み書き。ランデブーメッセージ、PCP、NAT-PMP は
// どれも整数をネットワークバイトオーダーで並べるので、境界の検査をここに集約する。
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace vocroz::network::detail {

/// バイト列を先頭から読む。範囲外を読もうとしたら以降の読み取りを空振りさせ、ok() を false にする。
/// 読み取りのたびに長さを確かめる分岐を書かずに済むようにするため。
/// スレッド安全性: 1 つのオブジェクトは 1 つのスレッドから使う。ヒープを確保しない。
class ByteReader {
public:
    /// @param bytes 読むバイト列。ByteReader より長く生きること
    explicit ByteReader(std::span<const std::uint8_t> bytes) noexcept : bytes_(bytes) {}

    /// n バイトを読み、その範囲を返す。足りなければ空の範囲を返して失敗にする。
    [[nodiscard]] std::span<const std::uint8_t> take(std::size_t n) noexcept {
        if (!ok_ || bytes_.size() - offset_ < n) {
            ok_ = false;
            return {};
        }
        const auto out = bytes_.subspan(offset_, n);
        offset_ += n;
        return out;
    }

    /// 1 バイトを読む。失敗したら 0 を返す。
    [[nodiscard]] std::uint8_t u8() noexcept {
        const auto b = take(1);
        return b.empty() ? 0 : b[0];
    }

    /// 2 バイトのビッグエンディアンの整数を読む。失敗したら 0 を返す。
    [[nodiscard]] std::uint16_t u16() noexcept {
        const auto b = take(2);
        if (b.empty()) {
            return 0;
        }
        return static_cast<std::uint16_t>((b[0] << 8) | b[1]);
    }

    /// 4 バイトのビッグエンディアンの整数を読む。失敗したら 0 を返す。
    [[nodiscard]] std::uint32_t u32() noexcept {
        const auto b = take(4);
        if (b.empty()) {
            return 0;
        }
        return (static_cast<std::uint32_t>(b[0]) << 24) | (static_cast<std::uint32_t>(b[1]) << 16) |
               (static_cast<std::uint32_t>(b[2]) << 8) | static_cast<std::uint32_t>(b[3]);
    }

    /// 以降の読み取りを失敗にする。値の検証に失敗したときに使う。
    void fail() noexcept {
        ok_ = false;
    }

    /// ここまでの読み取りがすべて成功していれば true を返す。
    [[nodiscard]] bool ok() const noexcept {
        return ok_;
    }

    /// すべてのバイトを過不足なく読み、失敗していなければ true を返す。
    [[nodiscard]] bool finished() const noexcept {
        return ok_ && offset_ == bytes_.size();
    }

private:
    std::span<const std::uint8_t> bytes_;
    std::size_t offset_ = 0;
    bool ok_ = true;
};

/// バイト列に先頭から書く。容量を超えたら以降の書き込みを空振りさせ、ok() を false にする。
/// スレッド安全性: 1 つのオブジェクトは 1 つのスレッドから使う。ヒープを確保しない。
class ByteWriter {
public:
    /// @param out 書き込み先。ByteWriter より長く生きること
    explicit ByteWriter(std::span<std::uint8_t> out) noexcept : out_(out) {}

    /// バイト列をそのまま書く。
    void bytes(std::span<const std::uint8_t> data) noexcept {
        if (!ok_ || out_.size() - offset_ < data.size()) {
            ok_ = false;
            return;
        }
        for (const std::uint8_t b : data) {
            out_[offset_++] = b;
        }
    }

    /// 1 バイトを書く。
    void u8(std::uint8_t value) noexcept {
        const std::uint8_t b[] = {value};
        bytes(b);
    }

    /// 2 バイトのビッグエンディアンの整数を書く。
    void u16(std::uint16_t value) noexcept {
        const std::uint8_t b[] = {static_cast<std::uint8_t>(value >> 8),
                                  static_cast<std::uint8_t>(value)};
        bytes(b);
    }

    /// 4 バイトのビッグエンディアンの整数を書く。
    void u32(std::uint32_t value) noexcept {
        const std::uint8_t b[] = {
            static_cast<std::uint8_t>(value >> 24), static_cast<std::uint8_t>(value >> 16),
            static_cast<std::uint8_t>(value >> 8), static_cast<std::uint8_t>(value)};
        bytes(b);
    }

    /// n バイトの 0 を書く（予約領域）。
    void zeros(std::size_t n) noexcept {
        for (std::size_t i = 0; i < n; ++i) {
            u8(0);
        }
    }

    /// ここまでの書き込みがすべて成功していれば true を返す。
    [[nodiscard]] bool ok() const noexcept {
        return ok_;
    }

    /// 書き込んだバイト数を返す。
    [[nodiscard]] std::size_t size() const noexcept {
        return offset_;
    }

private:
    std::span<std::uint8_t> out_;
    std::size_t offset_ = 0;
    bool ok_ = true;
};

}  // namespace vocroz::network::detail
