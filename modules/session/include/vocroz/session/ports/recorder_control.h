// SPDX-License-Identifier: AGPL-3.0-only
//
// マルチトラック録音を始めて止めるポート。pa-engine が、テイクのフォルダとファイル（media）、
// 録音タップ（audio-core の RecordingTrack）、ディスク I/O スレッドを組み立てて実装する
// （docs/adr/0043-recording-and-media-player.md）。
// session は、何を、いつ、どんな名前で録るかと、失敗したときにどうするかだけを決める。
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "vocroz/audio_core/audio_file_error.h"
#include "vocroz/audio_core/audio_file_format.h"
#include "vocroz/common/result.h"
#include "vocroz/control_protocol/states.h"

namespace vocroz::session {

/// 録音のトラック 1 本の録り方。
struct RecordingTrackPlan {
    /// 録る対象（ストリップ、"master"、バスの entity_id）。
    std::string source_id;
    /// テイクのフォルダの中のファイル名（拡張子を含む）。
    std::string file_name;

    friend bool operator==(const RecordingTrackPlan&, const RecordingTrackPlan&) = default;
};

/// 録音の録り方。session が設定から組み立てる。
struct RecordingPlan {
    /// メディアフォルダ（なければ作る）。
    std::string folder;
    audio_core::AudioFileFormat format = audio_core::AudioFileFormat::kWav;
    /// トラック（1 本以上）。この順にファイルを開く。
    std::vector<RecordingTrackPlan> tracks;

    friend bool operator==(const RecordingPlan&, const RecordingPlan&) = default;
};

/// 録音中の状態。
struct RecordingStatus {
    /// トラックごとの、ディスクの書き込みが間に合わず無音で埋めたフレーム数（plan の順）。
    std::vector<std::uint64_t> dropped_frames;
    /// どれかのトラックの書き出しが失敗していれば、その理由。
    std::optional<audio_core::AudioFileError> failure;
};

/// マルチトラック録音。
/// スレッド安全性: コントロールスレッドからだけ呼ぶ。start() はファイルを開く処理でブロックしうる。
class RecorderControl {
public:
    virtual ~RecorderControl() = default;

    /// テイクのフォルダを作り、すべてのトラックのファイルを開いてから、すべての録音タップを
    /// 1 つのグラフで足す。1 つでも開けなければ、開いたファイルを消して何も始めない。
    /// @param plan 録り方
    /// @return 作ったテイクのフォルダの名前。失敗なら理由（FOLDER_UNAVAILABLE、DISK_FULL、
    ///         IO_ERROR）
    [[nodiscard]] virtual common::Result<std::string, control_protocol::RecorderResult> start(
        const RecordingPlan& plan) = 0;

    /// 録音タップを外し、残りを書いてからファイルを閉じる（閉じるのは後でディスク I/O スレッドが
    /// 行い、待たない）。録音していなければ何もしない。
    virtual void stop() = 0;

    /// 録音中の欠落と失敗を返す。録音していなければ空の状態。
    [[nodiscard]] virtual RecordingStatus status() const = 0;
};

}  // namespace vocroz::session
