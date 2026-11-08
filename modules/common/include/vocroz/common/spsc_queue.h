// SPDX-License-Identifier: AGPL-3.0-only
//
// 単一生産者・単一消費者（SPSC）のロックフリーキュー。
// フェーダーの値などのパラメーターを、コントロールスレッドからオーディオスレッドへ渡すために使う
// （AGENTS.md 6.1）。オーディオスレッドではヒープ確保とブロッキング同期が使えないため
// （AGENTS.md 第 9 節）、固定長のリングバッファとアトミックな添字だけで実装する。
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>
#include <type_traits>

namespace vocroz::common {

/// 固定長の SPSC ロックフリーキュー。
/// 生産者スレッドと消費者スレッドはそれぞれ 1 つに限る。try_push() は生産者だけ、
/// try_pop() は消費者だけが呼ぶ。どちらもブロックせず、ヒープを確保しない。
/// 要素の領域はオブジェクト内に持つので、オブジェクトの生成と破棄はオーディオスレッド外で行う。
/// @tparam T        要素の型。コピーでヒープを確保しないよう、トリビアルにコピー可能な型に限る
/// @tparam Capacity 格納できる要素数。添字の剰余をビット演算で求めるため 2 のべき乗に限る
// MSVC は alignas によるパディングを警告 C4324 で報告する。偽共有を避けるための意図した
// パディングなので、このクラスに限って抑制する。
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324)
#endif
template <typename T, std::size_t Capacity>
class SpscQueue {
    static_assert(std::is_trivially_copyable_v<T>,
                  "要素のコピーでヒープを確保しないよう、トリビアルにコピー可能な型に限る");
    static_assert(std::is_default_constructible_v<T>, "リングバッファを事前に構築するため");
    static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0, "Capacity は 2 のべき乗にする");
    static_assert(std::atomic<std::size_t>::is_always_lock_free,
                  "オーディオスレッドでロックを取らないよう、添字の更新はロックフリーに限る");

public:
    SpscQueue() = default;
    // アトミックな添字を持つので、コピーもムーブもできない。
    SpscQueue(const SpscQueue&) = delete;
    SpscQueue& operator=(const SpscQueue&) = delete;
    SpscQueue(SpscQueue&&) = delete;
    SpscQueue& operator=(SpscQueue&&) = delete;
    ~SpscQueue() = default;

    /// 要素を末尾に追加する。
    /// @param item 追加する要素
    /// @return 追加できたら true。キューが満杯なら何もせず false
    /// スレッド安全性: 生産者スレッドだけが呼び出す。オーディオスレッドから呼び出し可能。
    [[nodiscard]] bool try_push(const T& item) noexcept {
        // 自分が書く添字は relaxed で読める。消費者が進めた head_ は acquire で読み、
        // 消費者が読み終えた領域だけを上書きするようにする。
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t head = head_.load(std::memory_order_acquire);
        if (tail - head == Capacity) {
            return false;
        }
        buffer_[tail & kIndexMask] = item;
        // release で公開し、消費者が tail_ を読んだ時点で要素の書き込みが見えるようにする。
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    /// 先頭の要素を取り出す。
    /// @return 取り出した要素。キューが空なら std::nullopt
    /// スレッド安全性: 消費者スレッドだけが呼び出す。オーディオスレッドから呼び出し可能。
    [[nodiscard]] std::optional<T> try_pop() noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        if (head == tail) {
            return std::nullopt;
        }
        const T item = buffer_[head & kIndexMask];
        // 読み終えてから release で添字を進め、生産者がこの領域を上書きできるようにする。
        head_.store(head + 1, std::memory_order_release);
        return item;
    }

    /// 格納できる要素数を返す。
    /// スレッド安全性: どのスレッドからも呼び出し可能。
    [[nodiscard]] static constexpr std::size_t capacity() noexcept {
        return Capacity;
    }

private:
    // 添字は単調増加させ、使うときにマスクする。満杯と空を区別するために 1 要素を空ける必要がない。
    // size_t の桁あふれも、Capacity が 2 のべき乗なので差とマスクの計算は正しく保たれる。
    static constexpr std::size_t kIndexMask = Capacity - 1;
    // 生産者と消費者が別々に書く添字を別のキャッシュラインに置き、偽共有で遅くならないようにする。
    // std::hardware_destructive_interference_size は処理系によって未定義や警告の対象になるため、
    // x86-64 と ARM64 の一般的なキャッシュラインの大きさである 64 バイトを使う。
    static constexpr std::size_t kCacheLineSize = 64;

    alignas(kCacheLineSize) std::atomic<std::size_t> head_{0};  // 消費者だけが書く
    alignas(kCacheLineSize) std::atomic<std::size_t> tail_{0};  // 生産者だけが書く
    alignas(kCacheLineSize) std::array<T, Capacity> buffer_{};
};
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

}  // namespace vocroz::common
