// SPDX-License-Identifier: AGPL-3.0-only
//
// 受信したメディアフレームを、到着時刻ではなくメディアタイムスタンプの順に並べ直すバッファ。
// 重複、順序逆転、遅着を判定し、再生位置より古いフレームは受け付けない。
// 再生の開始時期、目標の深さ、ドリフト補正は PlayoutStream
// が決め、このクラスは並べ替えと取り出しだけを担う。
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "vocroz/transport/media_frame.h"

namespace vocroz::transport {

/// フレームを投入した結果。
enum class InsertResult : std::uint8_t {
    kAccepted,   ///< 格納した
    kDuplicate,  ///< 同じタイムスタンプのフレームがすでにある。破棄した
    kLate,       ///< 再生位置より古い。古い音を再生しないよう破棄した
    kMisaligned,  ///< タイムスタンプがフレーム長の整数倍の位置にない。破棄した
    kDiscontinuity,  ///< 再生位置からバッファの容量以上先にある。格納済みのフレームを捨て、このフレームから並べ直した
};

/// フレームを取り出した結果の種類。
enum class PopKind : std::uint8_t {
    kFrame,  ///< 再生位置のフレームがあった
    kMissing,  ///< 再生位置のフレームはないが、より先のフレームはある（欠損）
    kEmpty,  ///< 再生位置以降のフレームが 1 つもない（枯渇）
};

/// 取り出した結果。kind が kFrame のときだけ frame が有効で、次に insert()
/// を呼ぶまで指す先が保たれる。
struct PopResult {
    PopKind kind = PopKind::kEmpty;
    const MediaFrame* frame = nullptr;
};

/// メディアタイムスタンプで整列するジッターバッファ。
/// 領域はコンストラクターで確保し、以降の操作はヒープを確保しない。
/// スレッド安全性: 1 つのオブジェクトを同時に 1 スレッドからだけ使う（PlayoutStream
/// がオーディオスレッドで使う）。
class JitterBuffer {
public:
    /// @param frame_samples   1 フレームのサンプル数（0 より大きい）
    /// @param capacity_frames
    /// 格納できるフレーム数。再生位置からこのフレーム数より先のフレームは並べ直しになる
    JitterBuffer(std::size_t frame_samples, std::size_t capacity_frames);

    /// フレームを格納する。
    /// @param frame 格納するフレーム。内容はコピーする
    /// @return 格納の結果
    /// スレッド安全性: オーディオスレッドから呼び出し可能。
    [[nodiscard]] InsertResult insert(const MediaFrame& frame) noexcept;

    /// 格納済みのうち最も古いフレームの位置から再生を始める。フレームがなければ何もしない。
    /// スレッド安全性: オーディオスレッドから呼び出し可能。
    void start() noexcept;

    /// 再生位置のフレームを取り出し、再生位置を 1 フレーム進める。
    /// 欠損や枯渇でも再生位置を進める。呼び出し側はその 1 フレームを PLC か無音で埋めるので、
    /// 再生位置は実時間と同じ速さで進み、あとから届いた遅着のフレームは kLate になる。
    /// 前提条件: started() が true。
    /// スレッド安全性: オーディオスレッドから呼び出し可能。
    [[nodiscard]] PopResult pop() noexcept;

    /// バッファの深さをサンプル数で返す。
    /// 再生中は再生位置から、再生前は格納済みの最も古いフレームの先頭から、最も新しいフレームの末尾までの長さ。
    /// フレームが 1 つもなければ 0。
    [[nodiscard]] std::size_t depth_samples() const noexcept;

    /// 深さが max_samples 以下になるまで、再生位置のフレームを捨てて再生位置を進める。
    /// 回線の停滞のあとに溜まったフレームを再生し続けて遅延が増えないようにするために使う。
    /// @param max_samples 残す深さの上限（サンプル数）
    /// @return 捨てたフレームと欠損の位置の数
    /// スレッド安全性: オーディオスレッドから呼び出し可能。
    std::size_t drop_until(std::size_t max_samples) noexcept;

    /// すべてのフレームを捨て、再生前の状態に戻す。新しい区間（ミュート解除後）の始まりで使う。
    /// スレッド安全性: オーディオスレッドから呼び出し可能。
    void reset() noexcept;

    /// 途絶が続いて無音にしたあと、再生前の状態に戻して溜め直す。
    /// reset()
    /// と違い、格納済みのフレームは残し、それまでの再生位置を下限として下限より古いフレームは kLate
    /// にする。 すでに再生した位置や無音で埋めた位置の古い音を、溜め直したあとに再生しないため。
    /// スレッド安全性: オーディオスレッドから呼び出し可能。
    void rebuffer() noexcept;

    /// 溜め直しの下限を進める。溜め直しの間もフレームが 1 つも届いていなければ、
    /// 無音で埋めているぶんだけ実時間が進んでいるので、呼び出し側が経過したサンプル数を渡す。
    /// フレームが届いたあとは下限を動かさない（溜めている最中のフレームを遅着にしないため）。
    /// @param samples 経過したサンプル数
    /// スレッド安全性: オーディオスレッドから呼び出し可能。
    void advance_floor(std::size_t samples) noexcept;

    /// 再生を始めていれば true を返す。
    [[nodiscard]] bool started() const noexcept {
        return started_;
    }

    /// 格納しているフレームの数を返す。
    [[nodiscard]] std::size_t stored_frames() const noexcept {
        return stored_;
    }

private:
    struct Slot {
        bool occupied = false;
        MediaFrame frame;
    };

    [[nodiscard]] Slot& slot_for(std::uint64_t timestamp) noexcept;
    [[nodiscard]] bool aligned(std::uint64_t timestamp) const noexcept;
    void store(Slot& slot, const MediaFrame& frame) noexcept;
    // timestamp より古いフレームをすべて捨てる。
    void discard_before(std::uint64_t timestamp) noexcept;
    // head_ を格納済みの最も古いフレームの位置まで進める。
    void seek_oldest() noexcept;

    std::size_t frame_samples_;
    std::vector<Slot> slots_;
    // capacity * frame_samples。再生位置からこの長さ以上先のフレームは格納できない。
    std::uint64_t window_samples_;

    bool started_ = false;
    // rebuffer() のあとだけ true。再生前でも floor_ より古いフレームを受け付けない。
    bool has_floor_ = false;
    std::uint64_t floor_ = 0;
    // 位置の整列の基準。区間の最初のフレームのタイムスタンプ。
    std::uint64_t origin_ = 0;
    // 再生前は格納済みの最も古いフレームの位置、再生中は次に再生する位置。
    std::uint64_t head_ = 0;
    // 格納済みの最も新しいフレームの先頭の位置。stored_ が 0 のときは意味を持たない。
    std::uint64_t newest_ = 0;
    std::size_t stored_ = 0;
};

}  // namespace vocroz::transport
