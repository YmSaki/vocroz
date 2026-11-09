// SPDX-License-Identifier: AGPL-3.0-only
//
// session の中だけで使う定数。値の根拠をそれぞれに書く。
#pragma once

#include <chrono>
#include <cstddef>

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

/// UI とスナップショットで使う固定の entity_id。UI のフェイクエンジンと同じ値にする。
inline constexpr const char* kSessionEntityId = "session";
inline constexpr const char* kMasterEntityId = "master";
inline constexpr const char* kClientEntityId = "client";
inline constexpr const char* kRoutingEntityId = "routing";

}  // namespace vocroz::session
