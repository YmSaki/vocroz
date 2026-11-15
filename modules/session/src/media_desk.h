// SPDX-License-Identifier: AGPL-3.0-only
//
// 録音とメディアプレイヤーの状態と、その遷移の規則（docs/adr/0043-recording-and-media-player.md）。
// - 録音の設定は、止めている間だけ変えられる
// - 録音はすべてのトラックのファイルを開けたときだけ始め、どれか 1 つの書き出しが失敗したら
//   全体を止めて知らせる（ライブの音声は止まらない）
// - 起動時は録音も再生も止めた状態から始め、ファイルを読み込んだ直後も鳴らさない（初期ミュート）
// - 再生できるのはメディアフォルダの一覧（ライブラリ）にあるファイルだけ
// - 流す先がルーティングからなくなったら一時停止して知らせる（別のバスへ暗黙に切り替えない）
// 録音と再生の実体（RecorderControl、MediaPlayerControl）と、ライブラリの走査（MediaLibrary）は
// ポート経由で使い、ここは状態と、いつ何を呼ぶかだけを持つ。
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "vocroz/common/ports/clock.h"
#include "vocroz/common/ports/logger.h"
#include "vocroz/common/result.h"
#include "vocroz/control_protocol/entities.h"
#include "vocroz/control_protocol/states.h"
#include "vocroz/session/ports/media_library.h"
#include "vocroz/session/ports/media_player_control.h"
#include "vocroz/session/ports/recorder_control.h"
#include "vocroz/session/ports/ui_event_sink.h"

namespace vocroz::session {

/// 録音とメディアプレイヤー。
/// スレッド安全性: コントロールスレッドからだけ使う。
class MediaDesk {
public:
    /// 注入するポート。
    struct Ports {
        RecorderControl& recorder;
        MediaPlayerControl& player;
        MediaLibrary& library;
        const common::Clock& clock;
        UiEventSink& ui;
        common::Logger& logger;
    };

    /// 録る対象の表示名を返す関数。録れない entity_id（ないストリップ、ないバス）なら
    /// std::nullopt。
    using SourceLabel = std::function<std::optional<std::string>(const std::string& entity_id)>;

    /// 流す先として使える entity_id（Master か、適用中のルーティングのバス）なら true を返す関数。
    using TargetCheck = std::function<bool(const std::string& entity_id)>;

    /// コマンドの結果。適用した後の revision か、拒否の理由。
    using CommandResult = common::Result<control_protocol::Revision, control_protocol::ErrorCode>;

    /// @param ports          注入するポート。このオブジェクトより長く生存させる
    /// @param default_folder 設定にメディアフォルダがないときに使うフォルダ
    MediaDesk(Ports ports, std::string default_folder);

    /// UI に送る録音の状態。
    [[nodiscard]] const control_protocol::Recorder& recorder() const noexcept {
        return recorder_;
    }
    /// UI に送るメディアプレイヤーの状態。
    [[nodiscard]] const control_protocol::MediaPlayer& player() const noexcept {
        return player_;
    }
    /// UI に送るライブラリ。
    [[nodiscard]] const std::vector<control_protocol::MediaFile>& library() const noexcept {
        return library_;
    }

    /// 保存した録音の設定を戻し、ライブラリを作る。録音は止めたまま。起動時に、ルーティングを
    /// 戻した後で 1 回だけ呼ぶ。revision は進めない。
    /// @param saved    保存した設定
    /// @param label_of 録る対象の確認。戻したルーティングにないバスは録る対象から外す
    void restore(const control_protocol::RecorderConfig& saved, const SourceLabel& label_of);

    /// 保存する録音の設定を返す。録る対象のうち、keep が true のもの（Master とバス）だけを残す。
    [[nodiscard]] control_protocol::RecorderConfig saved_config(const TargetCheck& keep) const;

    /// 録音の設定を変える。folder が空なら既定のメディアフォルダにする。
    /// @return 録音中なら INVALID_STATE、録れない対象か重複があれば NOT_FOUND
    [[nodiscard]] CommandResult set_recorder_config(control_protocol::RecorderConfig config,
                                                    const SourceLabel& label_of);

    /// 録音を始めるか止める。始められなかったときもコマンドは受け付け、STOPPED のまま
    /// last_result で原因を知らせる。
    /// @return 録る対象が 1 つもなければ（ないストリップだけのときを含む）INVALID_STATE
    [[nodiscard]] CommandResult set_recording(bool recording, const SourceLabel& label_of);

    /// ファイルを読み込む。空のパスはファイルを外す。開けなかったときもコマンドは受け付け、
    /// ファイルなしのまま last_result で原因を知らせる。
    /// @return ライブラリにないパスなら NOT_FOUND
    [[nodiscard]] CommandResult load(const std::string& path);

    /// 再生、一時停止、停止。
    /// @param transport 操作
    /// @param is_target 流す先の確認。流す先がルーティングからなくなっていれば再生を始めない
    /// @return ファイルがないか、流す先がないのに再生しようとしたら INVALID_STATE
    [[nodiscard]] CommandResult set_transport(control_protocol::MediaTransport transport,
                                              const TargetCheck& is_target);

    /// 再生位置を移す。
    /// @return ファイルがなければ INVALID_STATE
    [[nodiscard]] CommandResult seek(std::uint64_t position_ms);

    /// 流す先を変える。
    /// @return 流す先として使えなければ NOT_FOUND
    [[nodiscard]] CommandResult set_target(const std::string& target, const TargetCheck& is_target);

    /// ライブラリを作り直して UI に知らせる。
    /// @return メディアプレイヤーの revision（ライブラリは観測値なので進めない）
    [[nodiscard]] CommandResult refresh_library();

    /// 録れなくなった対象（片付けたストリップ、ルーティングから外したバス）を、録音の設定から外す。
    /// entity_id は振り直すので、残すと後で同じ ID になった別の対象を録ってしまう。録音中の
    /// トラックはそのまま無音を録り続ける。
    void prune_sources(const SourceLabel& label_of);

    /// 適用中のルーティングが変わった。流す先がなくなっていたら一時停止して知らせる。
    void on_routing_applied(const TargetCheck& is_target);

    /// 録音の欠落と失敗、再生の位置とファイルの終わりを拾う。tick() から呼ぶ。
    void tick();

private:
    void publish_recorder();
    void publish_player();
    void publish_library();
    void notice(control_protocol::NoticeSeverity severity, const char* code);
    // 録音を止め、最後の欠落と経過時間を状態に写す。revision と通知は呼び出し側。
    void stop_recording();
    // 読み込んだファイルの情報を空にする。
    void clear_file();
    // ライブラリを作り直す。
    void rebuild_library();
    // 録音中のトラックの欠落を、状態に写す。
    void update_dropped(const RecordingStatus& status);
    // 録音の経過時間を、状態に写す。
    void update_elapsed();
    // 再生位置を ms で状態に写す。
    void update_position(std::uint64_t frames);

    Ports ports_;
    std::string default_folder_;
    control_protocol::Recorder recorder_;
    control_protocol::MediaPlayer player_;
    std::vector<control_protocol::MediaFile> library_;
    // 録音を始めた時刻。録音していなければ std::nullopt。
    std::optional<common::Clock::TimePoint> recording_since_;
    std::optional<common::Clock::TimePoint> last_recorder_publish_;
    std::optional<common::Clock::TimePoint> last_position_publish_;
    // 読み込んだファイルの長さ（48 kHz のフレーム数）。
    std::uint64_t length_frames_ = 0;
};

}  // namespace vocroz::session
