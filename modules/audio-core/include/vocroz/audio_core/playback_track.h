// SPDX-License-Identifier: AGPL-3.0-only
//
// 再生トラック 1 本（メディアプレイヤー）。ディスク I/O スレッドが fill() でファイルを読んで
// デコードしたサンプルをロックフリーのリングバッファに先読みし、音声グラフの再生ノード
// （NodeKind::kPlayer）がオーディオスレッドから render() でそれを読むだけにする
// （docs/adr/0009-beta-module-layout.md）。先読みが間に合わなければ、待たずに無音で埋める。
// 再生の開始、一時停止、シークは 10ms のフェードを掛けて切り替え、クリックノイズを出さない
// （docs/adr/0031-recording-and-playback-nodes.md）。
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

#include "vocroz/audio_core/audio_file_error.h"
#include "vocroz/audio_core/constants.h"
#include "vocroz/audio_core/ports/audio_file_reader.h"
#include "vocroz/audio_core/realtime.h"
#include "vocroz/common/result.h"

namespace vocroz::audio_core {

/// 再生トラック。std::shared_ptr で NodeSpec::make_player() に渡す。
/// 構築直後は停止中で、位置はファイルの先頭。play() を呼ぶまで音を出さない（初期ミュート）。
/// 再生ノードは停止中もグラフに置いたままにする。シークの反映（古い先読みを捨てる処理）は
/// 再生ノードの処理の中で行うため。
///
/// スレッド安全性: 構築と破棄はオーディオスレッド外。play()、pause()、seek() はコントロール
/// スレッド、fill() はディスク I/O スレッド、render() はオーディオスレッドだけが呼ぶ。
/// 取得関数はどのスレッドからも呼び出し可能。
class PlaybackTrack {
public:
    /// @param channel_count チャンネル数（1 か 2。範囲外は近い方に丸める）。読むファイルの
    /// AudioFileReader::channel_count() と同じにする。再生ノードの出力のチャンネル数になる
    /// @param buffer_frames 先読みの長さ（フレーム）
    explicit PlaybackTrack(std::size_t channel_count,
                           std::size_t buffer_frames = kDefaultPlaybackBufferFrames);

    PlaybackTrack(const PlaybackTrack&) = delete;
    PlaybackTrack& operator=(const PlaybackTrack&) = delete;
    PlaybackTrack(PlaybackTrack&&) = delete;
    PlaybackTrack& operator=(PlaybackTrack&&) = delete;
    ~PlaybackTrack();

    /// チャンネル数を返す。
    [[nodiscard]] std::size_t channel_count() const noexcept;

    /// 再生を始める（フェードイン）。ファイルの終わりにいるなら無音のまま。
    /// スレッド安全性: コントロールスレッドから呼ぶ。ブロックしない。
    void play() noexcept;

    /// 再生を止める（フェードアウト）。位置はそのまま残る。
    /// スレッド安全性: コントロールスレッドから呼ぶ。ブロックしない。
    void pause() noexcept;

    /// 再生する位置を移す。次の fill() で読み直し、再生中ならフェードアウト → 移動 →
    /// フェードインの順で切り替わる。
    /// @param frame ファイルの先頭からのフレーム数（kSampleRate の時計）
    /// スレッド安全性: コントロールスレッドから呼ぶ。ブロックしない。
    void seek(std::uint64_t frame) noexcept;

    /// 先読みに空きがあるぶんだけファイルを読む。シークの要求があれば先に位置を移す。
    /// ファイルの終わりに達したら、次のシークまで読まない。
    /// @param reader 読み込み元
    /// @return 先読みに足したフレーム数。失敗なら AudioFileError（先読み済みのぶんは再生を続ける）
    /// スレッド安全性: ディスク I/O スレッドだけが呼ぶ。ブロッキング I/O を含む。
    [[nodiscard]] common::Result<std::size_t, AudioFileError> fill(AudioFileReader& reader);

    /// 1 ブロックぶんの再生音を書く。停止中と先読み切れのぶんは無音にする。
    /// @param channels 各チャンネルの書き込み先（channel_count() 本、各 frames サンプル以上）
    /// @param frames フレーム数（kBlockSize 以下）
    /// スレッド安全性: オーディオスレッドから呼び出し可能。ブロックせず、ヒープを確保しない。
    void render(float* const* channels, std::size_t frames) noexcept VOCROZ_NONBLOCKING;

    /// 再生を指示されていれば true を返す（ファイルの終わりで止まっていても true のまま）。
    [[nodiscard]] bool is_playing() const noexcept;

    /// 再生位置がファイルの終わりに達していれば true を返す。session はこれを見て停止の表示にする。
    [[nodiscard]] bool at_end() const noexcept;

    /// 最後に再生したサンプルの次の位置（ファイルの先頭からのフレーム数）を返す。
    [[nodiscard]] std::uint64_t position_frames() const noexcept;

    /// 再生中に先読みが間に合わず、無音で埋めたフレーム数の通算を返す。
    [[nodiscard]] std::uint64_t underrun_frames() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

}  // namespace vocroz::audio_core
