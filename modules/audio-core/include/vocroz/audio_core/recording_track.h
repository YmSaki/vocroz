// SPDX-License-Identifier: AGPL-3.0-only
//
// 録音トラック 1 本。音声グラフの録音タップ（NodeKind::kRecordTap）が、オーディオスレッドから
// ロックフリーのリングバッファにサンプルを書くだけにし、ファイルへの書き出しはディスク I/O
// スレッドが drain() で行う（docs/adr/0009-beta-module-layout.md）。
// ディスクの書き込みが遅れてリングバッファが溢れても、オーディオスレッドは待たずにそのブロックを
// 捨て、捨てたフレーム数を記録する。ディスク I/O スレッドは捨てた位置に同じ長さの無音を書くので、
// 欠落があってもファイルの長さと時間軸は他のトラックとそろったままになる
// （docs/adr/0031-recording-and-playback-nodes.md）。
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

#include "vocroz/audio_core/audio_file_error.h"
#include "vocroz/audio_core/constants.h"
#include "vocroz/audio_core/ports/audio_file_writer.h"
#include "vocroz/audio_core/realtime.h"
#include "vocroz/common/result.h"

namespace vocroz::audio_core {

/// 録音トラック。std::shared_ptr で NodeSpec::make_record_tap() に渡す。
///
/// 録音の開始と停止は、録音タップを含むグラフの適用で表す。タップがグラフにある間、毎ブロック
/// push() が呼ばれる。複数のトラックのタップを 1 つのグラフで足し外しすれば、すべてのトラックが
/// 同じブロックから始まり、同じブロックで終わる。
///
/// 停止の手順（session が行う）: タップを外したグラフを適用し、Mixer::is_graph_settled() が true に
/// なったら finish() を呼ぶ。
///
/// スレッド安全性: 構築と破棄はオーディオスレッド外。push() はオーディオスレッドだけ、drain() は
/// ディスク I/O スレッドだけが呼ぶ。それ以外の取得関数はどのスレッドからも呼び出し可能。
class RecordingTrack {
public:
    /// @param channel_count チャンネル数（1 か 2。範囲外は近い方に丸める）
    /// @param buffer_frames
    /// リングバッファの長さ（フレーム）。ディスクがこの時間を超えて止まると欠落する
    explicit RecordingTrack(std::size_t channel_count,
                            std::size_t buffer_frames = kDefaultRecordingBufferFrames);

    RecordingTrack(const RecordingTrack&) = delete;
    RecordingTrack& operator=(const RecordingTrack&) = delete;
    RecordingTrack(RecordingTrack&&) = delete;
    RecordingTrack& operator=(RecordingTrack&&) = delete;
    ~RecordingTrack();

    /// チャンネル数を返す。録音タップの入力のチャンネル数になる。
    [[nodiscard]] std::size_t channel_count() const noexcept;

    /// 1 ブロックのサンプルをリングバッファに書く。空きが足りなければ書かずに欠落として数える。
    /// 書き出しが失敗した後は何もしない。
    /// @param channels 各チャンネルの先頭へのポインター（channel_count() 本）
    /// @param frames フレーム数（kBlockSize 以下）
    /// スレッド安全性: オーディオスレッドから呼び出し可能。ブロックせず、ヒープを確保しない。
    void push(const float* const* channels, std::size_t frames) noexcept VOCROZ_NONBLOCKING;

    /// リングバッファにあるサンプルを、欠落を無音で埋めながら writer に書き出す。
    /// 書き出しが一度失敗したら、以降はサンプルを捨てて同じエラーを返し続ける
    /// （容量不足のディスクに書き続けず、ライブ音声への影響もないようにする）。
    /// @param writer 書き出し先
    /// @return 書き出したフレーム数（欠落を埋めた無音を含む）。失敗なら AudioFileError
    /// スレッド安全性: ディスク I/O スレッドだけが呼ぶ。ブロッキング I/O を含む。
    [[nodiscard]] common::Result<std::size_t, AudioFileError> drain(AudioFileWriter& writer);

    /// 録音を終える。残りのサンプルを書き出し、最後に続いていた欠落を無音で埋めてから、
    /// AudioFileWriter::finish() でファイルを閉じる。書き出しが失敗していても、そこまでに書けた
    /// ぶんを再生できるよう、ファイルは閉じる。
    /// 前提条件: このトラックの録音タップを含むグラフが、すべて破棄されている
    /// （Mixer::is_graph_settled() が true）。以降 push() は呼ばれない。
    /// @param writer 書き出し先
    /// @return 書き出したフレーム数（欠落を埋めた無音を含む）。失敗なら AudioFileError
    /// スレッド安全性: ディスク I/O スレッドだけが呼ぶ。ブロッキング I/O を含む。
    [[nodiscard]] common::Result<std::size_t, AudioFileError> finish(AudioFileWriter& writer);

    /// 通算で欠落したフレーム数を返す。0 でなければ、録音の一部が無音に置き換わっている。
    [[nodiscard]] std::uint64_t dropped_frames() const noexcept;

    /// 書き出しが失敗していれば、その理由を返す。
    [[nodiscard]] std::optional<AudioFileError> failure() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

}  // namespace vocroz::audio_core
