// SPDX-License-Identifier: AGPL-3.0-only
//
// 音声グラフの記述（値型）。
// コントロールスレッドでノードと結線を記述し、Mixer::apply_graph() に渡すと、
// オーディオスレッド外で検証してレンダーグラフを構築し、オーディオスレッドへスワップで渡す。
// Alpha の固定ミキサー（fixed_mixer_layout.h）も、Beta1 のルーティングも、この記述の 1
// 構成として作る。
#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "vocroz/audio_core/playback_track.h"
#include "vocroz/audio_core/ports/audio_sink.h"
#include "vocroz/audio_core/ports/audio_source.h"
#include "vocroz/audio_core/ports/audio_stream_sink.h"
#include "vocroz/audio_core/ports/midi_source.h"
#include "vocroz/audio_core/ports/plugin_processor.h"
#include "vocroz/audio_core/recording_track.h"

namespace vocroz::audio_core {

/// グラフ内のノードの識別子。1 つのグラフの中で一意にする。
struct NodeId {
    std::uint32_t value = 0;  ///< 識別子の値
    friend constexpr auto operator<=>(NodeId, NodeId) = default;
};

/// フェーダー（ゲインとミュート）の識別子。0 から MixerConfig::fader_capacity 未満。
/// フェーダーの状態はグラフの外（Mixer）が持つので、グラフを差し替えても値とランプが続く。
struct FaderId {
    std::uint16_t value = 0;  ///< 識別子の値
    friend constexpr auto operator<=>(FaderId, FaderId) = default;
};

/// メーターの識別子。0 から MixerConfig::meter_capacity 未満。
/// フェーダーと同じく、メーターの状態はグラフの外（Mixer）が持つ。
struct MeterId {
    std::uint16_t value = 0;  ///< 識別子の値
    friend constexpr auto operator<=>(MeterId, MeterId) = default;
};

/// ノードの種類。チャンネル数は種類で決まる。
enum class NodeKind {
    /// AudioSource からサンプルを取り出す。入力を持たない。チャンネル数は
    /// AudioSource::channel_count()（1 か 2）
    kSource,
    /// チャンネルストリップ。手動ディレイ → プリフェーダーのメーター → フェーダー。
    /// チャンネル数は NodeSpec::channels（1 か 2）
    kStrip,
    kBus,  ///< ステレオのバス。入力の加算 → フェーダー → ポストフェーダーのメーター
    kDelay,  ///< ステレオの遅延ノード。フィードバック経路（ループ）を通してよい唯一のノード
    kLimiter,  ///< ステレオの安全リミッター
    kOutput,  ///< 出力デバイスへの書き出し（ステレオ）。メーターは最終出力を測る
    /// プラグイン（VST3 のエフェクトと VSTi）。チャンネル数は PluginBusLayout で決まり、
    /// 報告された遅延はプラグイン遅延補正（PDC）の対象になる
    kPlugin,
    /// 録音タップ。入力の加算を RecordingTrack に書く。チャンネル数は RecordingTrack で決まり、
    /// 出力を持たない（後段へつながない）
    kRecordTap,
    /// 再生ノード（メディアプレイヤー）。PlaybackTrack の再生音を出す。入力を持たず、
    /// 出力のチャンネル数は PlaybackTrack で決まる
    kPlayer,
    /// 返送タップ。ステレオの入力を加算してモノラルにダウンミックスし（L と R の平均）、
    /// AudioSink に渡す。出力を持たない（後段へつながない）。子機への返送（Minus-Me）に使う
    /// （minus_me.h、docs/adr/0036-return-monitor.md）
    kReturnTap,
    /// ストリームタップ。ステレオの入力を加算して AudioStreamSink に渡す。出力を持たない
    /// （後段へつながない）。SRTP 外部出力に使う（docs/adr/0040-srtp-external-output.md）
    kStreamTap,
};

/// ノードの記述。各種類のファクトリー関数で作る。
struct NodeSpec {
    NodeId id;                            ///< ノードの識別子
    NodeKind kind = NodeKind::kOutput;    ///< ノードの種類
    std::shared_ptr<AudioSource> source;  ///< kSource のときのサンプルの取り出し先
    std::optional<FaderId> fader;         ///< kStrip、kBus のフェーダー
    std::optional<MeterId> meter;         ///< メーター（任意）
    /// kStrip: 手動同期オフセット（0〜kMaxInputDelaySamples）。
    /// kDelay: 遅延（kMinFeedbackDelaySamples〜kMaxFeedbackDelaySamples）。それ以外は使わない。
    std::size_t delay_samples = 0;
    /// kStrip: チャンネル数（1 か 2。範囲外は近い方に丸める）。ステレオのソースはステレオの
    /// ストリップにつなぐ。それ以外は使わない。
    std::size_t channels = 1;
    /// kOutput: 書き込む出力デバイスのチャンネルの組の先頭（0 始まり）。L をこのチャンネル、
    /// R を次のチャンネルに書く。kMaxOutputChannels - 1 未満。それ以外は使わない。
    std::size_t device_channel = 0;
    /// kPlugin のときの処理の呼び出し先。
    std::shared_ptr<PluginProcessor> plugin;
    /// kPlugin: 入出力のチャンネル数。make_plugin() が PluginProcessor::layout() から写す。
    PluginBusLayout plugin_layout;
    /// kPlugin: 処理の遅延（0〜kMaxLatencyCompensationSamples）。make_plugin() が
    /// PluginProcessor::latency_samples() から写す。PDC はこの値で計算する。
    std::size_t latency_samples = 0;
    /// kRecordTap のときの書き込み先。
    std::shared_ptr<RecordingTrack> recording;
    /// kPlayer のときの読み込み元。
    std::shared_ptr<PlaybackTrack> playback;
    /// kReturnTap のときのサンプルの渡し先。
    std::shared_ptr<AudioSink> sink;
    /// kStreamTap のときのサンプルの渡し先。
    std::shared_ptr<AudioStreamSink> stream_sink;

    /// ソースノードを作る。
    /// @param id ノードの識別子
    /// @param source サンプルの取り出し先（null にしない）
    [[nodiscard]] static NodeSpec make_source(NodeId id, std::shared_ptr<AudioSource> source);

    /// チャンネルストリップを作る。
    /// @param id ノードの識別子
    /// @param fader フェーダー
    /// @param meter プリフェーダーのメーター（付けないなら std::nullopt）
    /// @param delay_samples 手動同期オフセット（サンプル）。0 なら遅らせない
    /// @param channels チャンネル数（1 か 2。範囲外は近い方に丸める）
    [[nodiscard]] static NodeSpec make_strip(NodeId id, FaderId fader, std::optional<MeterId> meter,
                                             std::size_t delay_samples = 0,
                                             std::size_t channels = 1);

    /// バスを作る。
    /// @param id ノードの識別子
    /// @param fader フェーダー
    /// @param meter ポストフェーダーのメーター（付けないなら std::nullopt）
    [[nodiscard]] static NodeSpec make_bus(NodeId id, FaderId fader, std::optional<MeterId> meter);

    /// 遅延ノードを作る。ループの中に置くと、そのループはフィードバック経路として扱われ、
    /// GraphDescription::feedback_approved が true のときだけ適用できる。
    /// @param id ノードの識別子
    /// @param delay_samples 遅延（kMinFeedbackDelaySamples〜kMaxFeedbackDelaySamples）
    [[nodiscard]] static NodeSpec make_delay(NodeId id, std::size_t delay_samples);

    /// 安全リミッターを作る。
    /// @param id ノードの識別子
    [[nodiscard]] static NodeSpec make_limiter(NodeId id);

    /// 出力ノードを作る。
    /// @param id ノードの識別子
    /// @param meter 最終出力のメーター（付けないなら std::nullopt）
    /// @param device_channel 書き込む出力デバイスのチャンネルの組の先頭
    [[nodiscard]] static NodeSpec make_output(NodeId id, std::optional<MeterId> meter,
                                              std::size_t device_channel = 0);

    /// プラグインノードを作る。チャンネル数と遅延は、この時点の plugin の値を写す。
    /// 挿入位置の結線の間に置く。挿入の判断は session が持つ。
    /// チャンネルストリップへの挿入は「ソース → プラグイン → ストリップ」。
    /// バスへの挿入は「バス → プラグイン → 後段」。
    /// @param id ノードの識別子
    /// @param plugin 処理の呼び出し先（null にしない）
    /// スレッド安全性: コントロールスレッドから呼ぶ（PluginProcessor の取得関数を呼ぶため）。
    [[nodiscard]] static NodeSpec make_plugin(NodeId id, std::shared_ptr<PluginProcessor> plugin);

    /// 録音タップを作る。録音したいストリップかバス（またはソース）からつなぐ。
    /// フェーダーの前を録るなら SendTap::kPreFader でつなぐ。録音タップへのプリフェーダーの結線は、
    /// 聞こえる音を作らないのでセンドレベルを付けなくてよい。
    /// 録音の開始と停止は、このノードを含むグラフの適用で表す（recording_track.h）。
    /// @param id ノードの識別子
    /// @param track 書き込み先（null にしない）
    [[nodiscard]] static NodeSpec make_record_tap(NodeId id, std::shared_ptr<RecordingTrack> track);

    /// 再生ノードを作る。出力を任意のバスかストリップへつなぐ。
    /// @param id ノードの識別子
    /// @param track 読み込み元（null にしない）
    [[nodiscard]] static NodeSpec make_player(NodeId id, std::shared_ptr<PlaybackTrack> track);

    /// 返送タップを作る。通常は add_minus_me_returns()（minus_me.h）が作る。
    /// @param id ノードの識別子
    /// @param sink サンプルの渡し先（null にしない）
    [[nodiscard]] static NodeSpec make_return_tap(NodeId id, std::shared_ptr<AudioSink> sink);

    /// ストリームタップを作る。送り出すバスのポストフェーダーの出力をつなぐ。
    /// @param id ノードの識別子
    /// @param sink サンプルの渡し先（null にしない）
    [[nodiscard]] static NodeSpec make_stream_tap(NodeId id, std::shared_ptr<AudioStreamSink> sink);
};

/// 結線が送り元のどの位置から信号を取り出すか。
enum class SendTap {
    kPostFader,  ///< 送り元のフェーダーの後（チャンネルのフェーダーとミュートに従う）
    /// 送り元のフェーダーの前（ストリップは手動ディレイの後）。チャンネルのフェーダーと
    /// ミュートに関わらず送るので、キューやモニターのミックスに使う。送り元はストリップかバスに限る
    kPreFader,
};

/// ノード間の結線（センド）。from の信号を、level のゲインを掛けて to の入力に加算する。
struct Connection {
    Connection() = default;

    /// @param source 送り元
    /// @param destination 送り先
    /// @param send_tap 取り出す位置
    /// @param send_level センドレベル。std::nullopt なら 0 dB で常に送る
    Connection(NodeId source, NodeId destination, SendTap send_tap = SendTap::kPostFader,
               std::optional<FaderId> send_level = std::nullopt) noexcept
        : from(source), to(destination), tap(send_tap), level(send_level) {}

    NodeId from;                        ///< 送り元
    NodeId to;                          ///< 送り先
    SendTap tap = SendTap::kPostFader;  ///< 取り出す位置
    /// センドレベル（ゲインとミュート）。std::nullopt なら 0 dB で常に送る。
    /// プリフェーダーのセンドは、新規参加時の初期ミュートをチャンネルのフェーダーで守れないので、
    /// 必ずセンドレベルを付ける（フェーダーはミュートで始まる）。
    std::optional<FaderId> level;
};

/// グラフ内の MIDI ソースの識別子。1 つのグラフの中で一意にする。NodeId とは別の名前空間。
struct MidiSourceId {
    std::uint32_t value = 0;  ///< 識別子の値
    friend constexpr auto operator<=>(MidiSourceId, MidiSourceId) = default;
};

/// MIDI ソース（MIDI デバイス 1 台、子機 1 台など）の記述。
/// MIDI は PCM の結線とは独立に流すので、ノードではなくグラフの外側の入力として記述する。
struct MidiSourceSpec {
    MidiSourceId id;                     ///< ソースの識別子
    std::shared_ptr<MidiSource> source;  ///< イベントの取り出し先（null にしない）
};

/// MIDI の経路。from のイベントを、プラグインノード to の process() に渡す。
/// 1 つのソースを複数のプラグインへ、複数のソースを 1 つのプラグインへつないでよい。
/// 経路を外すと、その経路で発音中だったノートには、外した後の最初のブロックで Note Off を送る。
struct MidiConnection {
    MidiSourceId from;  ///< 送り元の MIDI ソース
    NodeId to;          ///< 送り先のプラグインノード
};

/// 音声グラフ全体の記述。ノードの並び順は処理順に影響しない（結線から決める）。
struct GraphDescription {
    std::vector<NodeSpec> nodes;                   ///< ノード
    std::vector<Connection> connections;           ///< 結線
    std::vector<MidiSourceSpec> midi_sources;      ///< MIDI ソース
    std::vector<MidiConnection> midi_connections;  ///< MIDI の経路
    /// 遅延ノードを通るループ（フィードバック経路）を、オペレーターが承認済みなら true。
    /// 遅延ノードを通らないループは、この値に関わらず適用できない。
    bool feedback_approved = false;
};

/// グラフの記述が不正な理由。
enum class GraphError {
    kDuplicateNodeId,    ///< 同じ NodeId のノードが複数ある
    kUnknownNode,        ///< 結線が存在しないノードを指している
    kInvalidConnection,  ///< ソースノードへの入力か、出力ノードからの出力か、同じ結線の重複がある
    kChannelMismatch,  ///< ステレオの出力をモノラルの入力（モノラルのストリップなど）へつないでいる
    kCycle,  ///< 遅延ノードを通らないループ（発振経路）がある。承認があっても適用できない
    kUnapprovedFeedback,  ///< 遅延ノードを通るループがあるが、オペレーターが承認していない
    kMissingSource,    ///< ソースノードに AudioSource がない
    kMissingFader,     ///< ストリップかバスにフェーダーがない
    kFaderOutOfRange,  ///< フェーダーの識別子が容量を超えている
    kMeterOutOfRange,  ///< メーターの識別子が容量を超えている
    kDuplicateFader,   ///< 同じフェーダーを複数のノードが使っている
    kDuplicateMeter,   ///< 同じメーターを複数のノードが使っている
    kInvalidSendTap,  ///< プリフェーダーのセンドの送り元がストリップでもバスでもない
    /// プリフェーダーのセンドにセンドレベルがない（録音タップへの結線を除く）
    kMissingSendLevel,
    kDelayOutOfRange,          ///< 手動ディレイか遅延ノードの遅延が範囲外
    kOutputChannelOutOfRange,  ///< 出力ノードのチャンネルが kMaxOutputChannels を超える
    kOverlappingOutputs,  ///< 複数の出力ノードが同じ出力デバイスのチャンネルに書く
    kMissingPlugin,  ///< プラグインノードに PluginProcessor がない
    kInvalidPluginLayout,  ///< プラグインのチャンネル数が範囲外（入力 0〜2、出力 1〜2）
    /// プラグインの遅延か、合流する経路の遅延の差が kMaxLatencyCompensationSamples を超える
    kLatencyOutOfRange,
    kMissingTrack,  ///< 録音タップに RecordingTrack が、再生ノードに PlaybackTrack がない
    kDuplicateMidiSource,  ///< 同じ MidiSourceId の MIDI ソースが複数ある
    kMissingMidiSource,    ///< MIDI ソースに MidiSource がない
    /// MIDI の経路が、存在しない MIDI
    /// ソースかノードを指しているか、送り先がプラグインノードでないか、 同じ経路が重複している
    kInvalidMidiConnection,
    kMissingSink,  ///< 返送タップに AudioSink が、ストリームタップに AudioStreamSink がない
};

}  // namespace vocroz::audio_core
