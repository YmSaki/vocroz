// SPDX-License-Identifier: AGPL-3.0-only
//
// オーディオスレッドとディスク I/O スレッドの間で音声のサンプルを受け渡す、単一生産者・
// 単一消費者（SPSC）のロックフリーのリングバッファ。
// 録音では、オーディオスレッドが書き、ディスク I/O スレッドが読んでファイルに書き出す。
// 再生では、ディスク I/O スレッドがデコードしたサンプルを書き、オーディオスレッドが読む。
// オーディオスレッドが相手のスレッドを待たないよう（AGENTS.md 第 9 節）、領域は構築時に確保し、
// 読み書きの位置はアトミックな添字だけで受け渡す。
// common::SpscQueue は 1 要素ずつ受け渡す固定長のキューで、ここではブロック単位のまとめた
// コピーと、実行時に決める容量（秒数）が要るので別に持つ。
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "vocroz/audio_core/realtime.h"

namespace vocroz::audio_core {

// MSVC は alignas によるパディングを警告 C4324 で報告する。偽共有を避けるための意図した
// パディングなので、このクラスに限って抑制する（common::SpscQueue と同じ）。
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324)
#endif
/// 1〜2 チャンネルのサンプルをフレーム単位で受け渡すリングバッファ。内部はインターリーブ形式。
/// 書き込みと読み込みの位置は、構築からの通算のフレーム数（単調増加）で表す。
/// スレッド安全性: write_*() と write_position() の更新は生産者スレッドだけ、read_*() と
/// skip_to() は消費者スレッドだけが呼ぶ。*_frames() と *_position() はどのスレッドからも
/// 呼び出し可能。構築と破棄はオーディオスレッド外で行う。
class SampleRingBuffer {
public:
    /// @param channel_count チャンネル数（1 か 2。範囲外は近い方に丸める）
    /// @param capacity_frames 格納できるフレーム数（1 以上。0 は 1 として扱う）
    SampleRingBuffer(std::size_t channel_count, std::size_t capacity_frames);

    SampleRingBuffer(const SampleRingBuffer&) = delete;
    SampleRingBuffer& operator=(const SampleRingBuffer&) = delete;
    SampleRingBuffer(SampleRingBuffer&&) = delete;
    SampleRingBuffer& operator=(SampleRingBuffer&&) = delete;
    ~SampleRingBuffer() = default;

    /// チャンネル数を返す。
    [[nodiscard]] std::size_t channel_count() const noexcept {
        return channel_count_;
    }

    /// 格納できるフレーム数を返す。
    [[nodiscard]] std::size_t capacity_frames() const noexcept {
        return capacity_frames_;
    }

    /// 書き込めるフレーム数を返す。
    /// スレッド安全性: 生産者スレッドが呼ぶと正確な値になる。他のスレッドでは目安。
    [[nodiscard]] std::size_t writable_frames() const noexcept VOCROZ_NONBLOCKING;

    /// 読めるフレーム数を返す。
    /// スレッド安全性: 消費者スレッドが呼ぶと正確な値になる。他のスレッドでは目安。
    [[nodiscard]] std::size_t readable_frames() const noexcept VOCROZ_NONBLOCKING;

    /// planar 形式のサンプルを、全フレームが入るときだけ書き込む。
    /// 録音で 1
    /// ブロックの一部だけを書くと、どこで欠けたかを記録できなくなるので、全部か無しにする。
    /// @param channels 各チャンネルの先頭へのポインター（channel_count() 本）
    /// @param frames 書き込むフレーム数
    /// @return 書き込めたら true。空きが足りなければ何も書かずに false
    /// スレッド安全性: 生産者スレッドだけが呼ぶ。オーディオスレッドから呼び出し可能。
    [[nodiscard]] bool write_planar(const float* const* channels,
                                    std::size_t frames) noexcept VOCROZ_NONBLOCKING;

    /// インターリーブ形式のサンプルを、空きのあるぶんだけ書き込む。
    /// @param interleaved 書き込むサンプル（フレーム数 × チャンネル数）
    /// @return 書き込んだフレーム数
    /// スレッド安全性: 生産者スレッドだけが呼ぶ。
    std::size_t write_interleaved(std::span<const float> interleaved) noexcept;

    /// 読めるぶんだけ planar 形式で読み出す。足りないぶんは書き込み先に触れない。
    /// @param channels 各チャンネルの書き込み先（channel_count() 本、各 frames サンプル以上）
    /// @param frames 読みたいフレーム数
    /// @return 読んだフレーム数
    /// スレッド安全性: 消費者スレッドだけが呼ぶ。オーディオスレッドから呼び出し可能。
    std::size_t read_planar(float* const* channels, std::size_t frames) noexcept VOCROZ_NONBLOCKING;

    /// 読めるぶんだけインターリーブ形式で読み出す。
    /// @param interleaved 書き込み先（フレーム数 × チャンネル数）
    /// @param max_frames 読む上限のフレーム数
    /// @return 読んだフレーム数
    /// スレッド安全性: 消費者スレッドだけが呼ぶ。
    std::size_t read_interleaved(std::span<float> interleaved, std::size_t max_frames) noexcept;

    /// 読む位置を position まで進め、それより前のサンプルを捨てる。
    /// 書き込み位置を超える値は、書き込み位置までとして扱う。現在の読み込み位置より前なら何もしない。
    /// @param position 通算のフレーム位置
    /// スレッド安全性: 消費者スレッドだけが呼ぶ。オーディオスレッドから呼び出し可能。
    void skip_to(std::uint64_t position) noexcept VOCROZ_NONBLOCKING;

    /// 通算で書き込んだフレーム数を返す。
    [[nodiscard]] std::uint64_t write_position() const noexcept VOCROZ_NONBLOCKING {
        return write_position_.load(std::memory_order_acquire);
    }

    /// 通算で読んだ（捨てたものを含む）フレーム数を返す。
    [[nodiscard]] std::uint64_t read_position() const noexcept VOCROZ_NONBLOCKING {
        return read_position_.load(std::memory_order_acquire);
    }

private:
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
                  "オーディオスレッドでロックを取らないよう、位置の更新はロックフリーに限る");

    // 通算のフレーム位置を、領域の先頭からのサンプルの添字に変える。
    [[nodiscard]] std::size_t sample_index(std::uint64_t position) const noexcept
        VOCROZ_NONBLOCKING;

    std::size_t channel_count_;
    std::size_t capacity_frames_;
    std::vector<float> samples_;  // capacity_frames_ × channel_count_。構築時に確保する

    // 生産者と消費者が別々に書く位置を別のキャッシュラインに置き、偽共有で遅くならないようにする。
    // 64 バイトの根拠は common::SpscQueue と同じ（x86-64 と ARM64 の一般的なキャッシュライン）。
    static constexpr std::size_t kCacheLineSize = 64;
    alignas(kCacheLineSize) std::atomic<std::uint64_t> write_position_{0};  // 生産者だけが書く
    alignas(kCacheLineSize) std::atomic<std::uint64_t> read_position_{0};  // 消費者だけが書く
};
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

}  // namespace vocroz::audio_core
