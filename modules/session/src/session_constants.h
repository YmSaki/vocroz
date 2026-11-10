// SPDX-License-Identifier: AGPL-3.0-only
//
// session の中だけで使う定数。値の根拠をそれぞれに書く。
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>

#include "vocroz/session/entity_ids.h"

namespace vocroz::session {

/// 招待の有効期間。仕様の設計値（30 分）。ランデブーサーバーの上限と同じで、
/// 期限が来たら親機は新しい招待を発行し直す（docs/adr/0022-session-domain.md）。
inline constexpr std::chrono::minutes kInviteLifetime{30};

/// RECONNECTING のまま LOST に移るまでの再接続の猶予。仕様の設計値（120 秒）。
/// 子機も同じ時間で再接続をあきらめ、RECONNECT_TIMEOUT で FAILED になる。
inline constexpr std::chrono::seconds kReconnectGrace{120};

/// 重複を見分けるために覚えておく request_id の数。
/// UI は ACK を待ってから次の離散操作を送るので、同時に未応答の要求は数個に収まる。
/// 再送や二重送信を見分けるには十分な余裕を見て 256 件にする。
inline constexpr std::size_t kRememberedRequestIds = 256;

/// 承認待ちの参加者の上限。承認パネルに並べて判断できる数を超える申請は、
/// 招待コードを知る第三者による申請の連打とみなして拒否する。
inline constexpr std::size_t kMaxPendingParticipants = 16;

/// 入力チャンネルストリップの上限。スナップショットが kMaxMessageBytes（64 KiB）に収まる数
/// （control-protocol の見積もりで 64 本で 20 KiB 程度）。Alpha の保証は 8 本。
inline constexpr std::size_t kMaxChannelStrips = 64;

/// 親機のフェーダー（チャンネルと Master）の上限（dB）。仕様のフェーダーの範囲（-∞〜+10 dB）。
inline constexpr double kMaxFaderGainDb = 10.0;

/// 子機の送信レベルの範囲（dB）。仕様の子機の画面の範囲（-60〜+12 dB）。
inline constexpr double kMinSendLevelDb = -60.0;
inline constexpr double kMaxSendLevelDb = 12.0;

/// 表示名（参加者名）の上限のバイト数（UTF-8）。ストリップの幅（80〜112 px）に収まる文字数より
/// 十分大きく、制御メッセージ 1 つ（最大 1166 バイト）に収まる値にする。
inline constexpr std::size_t kMaxDisplayNameBytes = 64;

/// 子機のマイクのストリーム ID。入力の申告を送らない子機を、親機はストリーム 0 のモノラルの
/// マイクとみなすので、マイクは同じ ID にする（docs/adr/0035-client-input-expansion.md）。
inline constexpr std::uint16_t kMicrophoneStreamId = 0;

/// 子機のアプリ音声のストリーム ID。
inline constexpr std::uint16_t kAppAudioStreamId = 1;

/// アプリ音声のチャンネル数。再生デバイスのミックスはステレオなので、そのまま送る。
inline constexpr std::uint8_t kAppAudioChannels = 2;

/// セッション名の上限のバイト数（UTF-8）。ステータス帯に出す長さとして十分な値にする。
inline constexpr std::size_t kMaxSessionNameBytes = 128;

/// ルーティングのバス（ユーザーバスとキューバス）の上限。返しを出演者の人数分と、サブグループや
/// エフェクトの送りを数本作れる数。
inline constexpr std::size_t kMaxRoutingBuses = 32;

/// 追加の物理出力の上限。1 台の出力デバイスのチャンネル（audio-core の kMaxOutputChannels = 64）の
/// うちメインの出力の 2 チャンネルを除いた残りを、ステレオの組で使い切れる数。
inline constexpr std::size_t kMaxRoutingOutputs = 31;

/// センドの上限。スナップショットは適用中とステージング中の構成を両方含むので、センド 1 本を
/// 120 バイトとして 2 つの構成で約 30 KiB に抑え、ストリップ 64 本と合わせても
/// kMaxMessageBytes（64 KiB）に収まる数にする。
inline constexpr std::size_t kMaxRoutingSends = 128;

/// フィードバック経路の遅延ノードの上限。ループは意図して作る特殊な経路なので数本で足りる。
inline constexpr std::size_t kMaxRoutingDelays = 8;

/// ルーティングの entity_id の接頭辞。UI が振る ID を、エンジンが振る ID（ch1、pending1、master
/// など）と衝突させないため、種類ごとに決めた接頭辞で始めさせる。
inline constexpr const char* kBusIdPrefix = "bus-";
inline constexpr const char* kOutputIdPrefix = "out-";
inline constexpr const char* kSendIdPrefix = "send-";
inline constexpr const char* kDelayIdPrefix = "delay-";
inline constexpr const char* kInsertIdPrefix = "fx-";

/// 1 つの挿す先（ストリップ、Master、バス）に直列につなぐ挿入の上限。
/// 1 本のチャンネルに EQ、コンプ、ディエッサー、リバーブなどを重ねても足りる数。
/// プラグインごとにブロックの処理時間が増えるので、際限なく重ねさせない。
inline constexpr std::size_t kMaxInsertsPerTarget = 8;

/// 挿入の総数の上限。プラグインのインスタンスはそれぞれメモリとオーディオスレッドの時間を使う。
/// スナップショットは適用中とステージング中の構成を両方含むので、挿入 1 つを 100 バイトとして
/// 2 つの構成で約 6 KiB に抑え、センドとストリップと合わせても kMaxMessageBytes（64 KiB）に
/// 収まる数にする（docs/adr/0042-plugin-insert.md）。
inline constexpr std::size_t kMaxInserts = 32;

/// 子機の MIDI の時刻を写すときに、「受信時刻 − 子機の時刻」の最小値を取り直す窓の長さ。
/// 1 つ前の窓と合わせて 2〜4 秒の最小値を使う。演奏中のネットワークの揺れ（数十 ms 程度の山）を
/// 見逃さない長さで、時計の進み方のずれ（100 ppm で 4 秒あたり 0.4ms）が溜まらない短さにする。
inline constexpr std::chrono::seconds kMidiTimelineWindow{2};

// UI とスナップショットで使う固定の entity_id（kSessionEntityId など）は、ミキサーの組み立て
// （pa-engine）もルーティングの送り元と送り先を引くのに使うので、公開ヘッダーに置いている。

}  // namespace vocroz::session
