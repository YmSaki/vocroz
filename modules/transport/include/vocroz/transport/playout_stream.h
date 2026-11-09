// SPDX-License-Identifier: AGPL-3.0-only
//
// 親機の受信側で、1 ストリームのメディアフレームを途切れない PCM に戻す。
// ストリームはモノラルかステレオで、チャンネル数は生成時に固定する。
// ネットワーク受信スレッドが enqueue() でフレームを渡し、
// オーディオスレッドが render() で PCM を取り出す。
// 内部では、適応型ジッターバッファでタイムスタンプ順に整列し、
// Opus でデコードし（欠損は PLC で補い）、クロックドリフト補正のリサンプラーで深さを目標に保つ。
// 新しいミュートの世代のフレームが届いたら新しい区間として初期化し、
// 停止前の古いデータを再生しない。
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include "vocroz/audio_core/realtime.h"
#include "vocroz/common/result.h"
#include "vocroz/transport/constants.h"
#include "vocroz/transport/media_frame.h"

namespace vocroz::transport {

/// 再生ストリームの設定。
/// 既定値は合奏向けの低遅延プロファイル（docs/adr/0002-opus-low-delay.md）。
struct PlayoutConfig {
    /// 送信側のフレーム長。
    FrameDuration frame_duration = FrameDuration::k5000Us;
    /// ジッターバッファの目標の深さの下限。
    /// 目標は下限から始め、枯渇が起きたら深くする。
    std::chrono::microseconds min_delay{10'000};
    /// ジッターバッファの目標の深さの上限。
    /// オペレーターが回線の状態に応じて広げる。
    /// 深さがこれを超えたら（回線の停滞のあとにまとめて届いたなど）、
    /// 古いフレームを捨てて目標まで浅くする。
    std::chrono::microseconds max_delay{20'000};
    /// チャンネル数（1 か 2）。送信側のストリームと同じ値にする。
    std::size_t channels = 1;
};

/// 再生ストリームを作れなかった原因。
enum class PlayoutError : std::uint8_t {
    kInvalidConfig,  ///< 設定の値が範囲外（下限が上限より大きい、1
                     ///< フレームより短い、上限が長すぎる、チャンネル数が 1 と 2 以外など）
    kCodecFailed,    ///< デコーダーを生成できなかった
};

/// 再生ストリームの統計。
/// テストと、将来の回線品質の表示に使う。
struct PlayoutStats {
    std::uint64_t frames_decoded = 0;    ///< デコードしたフレーム
    std::uint64_t frames_concealed = 0;  ///< PLC で補間したフレーム
    std::uint64_t frames_late = 0;       ///< 再生位置より古くて捨てたフレーム
    std::uint64_t frames_duplicate = 0;  ///< 重複して捨てたフレーム
    std::uint64_t frames_misaligned = 0;  ///< タイムスタンプの位置が不正で捨てたフレーム
    std::uint64_t frames_stale_epoch = 0;  ///< 古いミュートの世代で捨てたフレーム
    std::uint64_t frames_dropped_overflow = 0;  ///< 深さが上限を超えて捨てたフレーム
    std::uint64_t frames_queue_full = 0;  ///< オーディオスレッドへのキューが満杯で捨てたフレーム
    std::uint64_t underruns = 0;  ///< 再生位置以降のフレームが 1 つもなかった回数
    std::uint64_t rebuffers = 0;  ///< 途絶が続いて無音にし、溜め直した回数
    std::uint64_t segments = 0;   ///< 新しい区間（ミュート解除後）を始めた回数
    std::size_t target_delay_samples = 0;  ///< 現在の目標の深さ（サンプル数）
    double depth_samples = 0.0;  ///< 直近の深さ（ジッターバッファとリサンプラーの合計）
    double rate_ratio = 1.0;      ///< 直近のドリフト補正の変換比
    double drift_estimate = 0.0;  ///< 推定したドリフト（変換比の 1 からのずれ）
    bool playing = false;  ///< 再生中なら true。溜めている間と無音の間は false
};

/// 1 ストリームの再生。
/// 生成はオーディオスレッド外で行い、以降の操作はヒープを確保しない。
/// スレッド安全性: enqueue() は 1 つの生産者スレッド（ネットワーク受信スレッド）だけが、
/// render() と stats() は 1 つの消費者スレッド（オーディオスレッド）だけが呼ぶ。
class PlayoutStream {
public:
    /// 再生ストリームを生成する。
    /// @param config 設定
    /// @return 生成したストリーム。設定が不正なら kInvalidConfig、
    /// デコーダーを作れなければ kCodecFailed
    /// スレッド安全性: オーディオスレッド外で呼ぶ（ヒープを確保する）。
    [[nodiscard]] static common::Result<std::unique_ptr<PlayoutStream>, PlayoutError> create(
        const PlayoutConfig& config);

    PlayoutStream(const PlayoutStream&) = delete;
    PlayoutStream& operator=(const PlayoutStream&) = delete;
    PlayoutStream(PlayoutStream&&) = delete;
    PlayoutStream& operator=(PlayoutStream&&) = delete;
    ~PlayoutStream();

    /// 受信したフレームをオーディオスレッドへ渡す。
    /// SignalMonitor::on_media_frame() が受け付けたフレームだけを渡す。
    /// @param frame 渡すフレーム。内容はコピーする
    /// @return 渡せたら true。キューが満杯なら捨てて false
    /// スレッド安全性: 生産者スレッドだけが呼ぶ。ブロックしない。
    bool enqueue(const MediaFrame& frame) noexcept;

    /// モノラルのストリームの再生する PCM を書き込む。
    /// 届いたフレームを取り込んでから、out の長さぶんを埋める。
    /// 再生できる音がない間（溜めている間、途絶が続いたあと）は無音を書き込む。
    /// @param out 書き込み先。長さはデバイスのコールバック長（64〜512 サンプルなど）でよく、
    /// 毎回変わってもよい
    /// エラー時の振る舞い: ステレオのストリームで呼ぶと無音を書き込み、ストリームは進めない。
    /// スレッド安全性: オーディオスレッドから呼び出し可能（ヒープを確保せず、ブロックしない）。
    void render(std::span<float> out) noexcept VOCROZ_NONBLOCKING;

    /// ステレオのストリームの再生する PCM を書き込む。振る舞いは render(out) と同じ。
    /// @param left  L の書き込み先
    /// @param right R の書き込み先。長さは left と同じにする
    /// エラー時の振る舞い: モノラルのストリームで呼ぶか、長さが違えば無音を書き込み、
    /// ストリームは進めない。
    /// スレッド安全性: オーディオスレッドから呼び出し可能（ヒープを確保せず、ブロックしない）。
    void render(std::span<float> left, std::span<float> right) noexcept VOCROZ_NONBLOCKING;

    /// チャンネル数（1 か 2）を返す。
    /// スレッド安全性: どのスレッドからも呼び出し可能。
    [[nodiscard]] std::size_t channels() const noexcept;

    /// 統計を返す。
    /// スレッド安全性: render() と同じスレッドから呼ぶ。
    [[nodiscard]] PlayoutStats stats() const noexcept;

    /// 実装の詳細。
    /// 定義は .cpp にだけあり、外からは使えない。
    struct Impl;
    /// create() の中からだけ構築できるようにするための型。
    /// コンストラクターが private なので外からは作れない。
    /// std::make_unique で構築するために、コンストラクター自体は public にしている。
    class ConstructionKey {
        friend class PlayoutStream;
        ConstructionKey() = default;
    };

    /// create() が使う。
    /// 外からは ConstructionKey を作れないので呼べない。
    PlayoutStream(ConstructionKey key, std::unique_ptr<Impl> impl) noexcept;

private:
    std::unique_ptr<Impl> impl_;
};

}  // namespace vocroz::transport
