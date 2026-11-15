// SPDX-License-Identifier: AGPL-3.0-only
//
// メディアプレイヤー（音声ファイル 1 つの再生）のポート。pa-engine が、ファイル（media）、
// 再生ノード（audio-core の PlaybackTrack）、ディスク I/O スレッドを組み立てて実装する
// （docs/adr/0043-recording-and-media-player.md）。
// session は、どのファイルを、どこへ、いつ鳴らすかだけを決める。
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "vocroz/audio_core/audio_file_error.h"
#include "vocroz/common/result.h"
#include "vocroz/control_protocol/states.h"

namespace vocroz::session {

/// 読み込んだファイルの情報。
struct MediaInfo {
    /// チャンネル数（1 か 2）。
    std::size_t channels = 0;
    /// 長さ（48 kHz のフレーム数）。
    std::uint64_t length_frames = 0;
};

/// 再生中の状態。
struct MediaPlayerStatus {
    /// 再生位置（48 kHz のフレーム数）。
    std::uint64_t position_frames = 0;
    /// ファイルの終わりまで鳴らし終えた。
    bool at_end = false;
    /// 読み込みが失敗していれば、その理由。
    std::optional<audio_core::AudioFileError> failure;
};

/// メディアプレイヤー。
/// スレッド安全性: コントロールスレッドからだけ呼ぶ。load() はファイルを開く処理でブロックしうる。
class MediaPlayerControl {
public:
    virtual ~MediaPlayerControl() = default;

    /// ファイルを開き、再生ノードを流す先へつなぐ。読み込んでいれば外してから開く。
    /// 開いた直後は止まっていて、play() を呼ぶまで鳴らない（初期ミュート）。
    /// @param file   開くファイル（メディアフォルダの中の、検証済みのパス）
    /// @param target 流す先（"master" かバスの entity_id）
    /// @return ファイルの情報。失敗なら理由（NOT_FOUND、UNSUPPORTED_FORMAT、IO_ERROR）で、
    ///         そのときはファイルなしのまま
    [[nodiscard]] virtual common::Result<MediaInfo, control_protocol::MediaResult> load(
        const std::string& file, const std::string& target) = 0;

    /// ファイルを外す。読み込んでいなければ何もしない。
    virtual void unload() = 0;

    /// 再生を始めるか、一時停止から続ける。10 ms でフェードインする。
    virtual void play() = 0;

    /// 一時停止する。10 ms でフェードアウトする。
    virtual void pause() = 0;

    /// 再生位置を移す。長さを超える位置は終わりとして扱う。
    /// @param frame 先頭からの位置（48 kHz のフレーム数）
    virtual void seek(std::uint64_t frame) = 0;

    /// 流す先を変える。
    /// @param target 流す先（"master" かバスの entity_id）
    virtual void set_target(const std::string& target) = 0;

    /// 再生位置と、ファイルの終わり、失敗を返す。読み込んでいなければ空の状態。
    [[nodiscard]] virtual MediaPlayerStatus status() const = 0;
};

}  // namespace vocroz::session
