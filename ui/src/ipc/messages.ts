// SPDX-License-Identifier: AGPL-3.0-only
// UI とエンジンの間でやり取りするメッセージの型。
//
// スキーマの正本は modules/control-protocol（C++）で、このファイルはその対になる定義（AGENTS.md 第 4.4 節）。
// 両者の食い違いは、modules/control-protocol/tests/data/ の全メッセージの JSON の例を C++ と UI の両方の
// テストで読んで見つける（docs/adr/0015-control-protocol-messages.md）。メッセージを変えるときは、
// C++ の型、JSON の例、このファイルを同じ PR で変更する。
//
// revision は、コマンドで変更できる値（ミュート、入力の選択など）が変わったときだけ進む。
// 接続状態や信号状態のように、エンジンが観測して変わるだけの値の変化では進まない。
//
// 状態の値は仕様書の正規名（UPPER_SNAKE_CASE）、フラグは lower_snake_case で受け取る。
// UI は正規名を画面に出さず、i18n の表示ラベルに変換して表示する。

import type { GainDb } from "../utils/db";

export type { GainDb };

/** 親機のセッション状態。 */
export type SessionState = "IDLE" | "CREATING" | "READY" | "ACTIVE" | "ENDING" | "ERROR";

/** 親機側から見た参加者の接続状態。PENDING の参加者はストリップを持たず、承認パネルにだけ出る。 */
export type ConnectionState =
  "PENDING" | "CONNECTING" | "CONNECTED" | "RECONNECTING" | "LOST" | "FAILED";

/** 信号状態。接続状態が CONNECTED のときだけ値を持つ。 */
export type SignalState = "RECEIVING" | "PAUSED" | "RESUMING" | "NO_SIGNAL";

/** 親機の出力デバイスの状態。 */
export type OutputDeviceState = "NONE" | "OPEN" | "LOST";

/** Master をミュートした主体。SAFETY はエンジンが安全のために設定したもの。 */
export type MasterMuteReason = "OPERATOR" | "SAFETY";

/** 子機の接続状態。 */
export type ClientState =
  | "DISCONNECTED"
  | "RESOLVING"
  | "AUTHENTICATING"
  | "WAITING_APPROVAL"
  | "CONNECTING"
  | "CONNECTED"
  | "RECONNECTING"
  | "FAILED";

/**
 * エンジンが返すエラーコード。UI はこのコードからローカライズ済みのメッセージを表示する。
 * 子機の FAILED と DISCONNECTED の理由、セッションとストリップの失敗の原因、コマンドの拒否理由に共通で使う。
 */
export type ErrorCode =
  | "REJECTED"
  | "INVITE_EXPIRED"
  | "INVITE_REVOKED"
  | "INVITE_INVALID"
  | "UNREACHABLE"
  | "RECONNECT_TIMEOUT"
  | "SESSION_ENDED"
  | "REVISION_CONFLICT"
  | "NOT_FOUND"
  | "INVALID_STATE"
  | "ENGINE_ERROR"
  | "DEVICE_UNAVAILABLE";

/** ルーティングのバスの種類。処理は同じで、表示と既定の送り方だけが違う。 */
export type BusKind = "USER" | "CUE";

/** センドを取り出す位置。PRE_FADER は送り元のフェーダーとミュートの前で、センドのミュートでだけ止まる。 */
export type SendTap = "POST_FADER" | "PRE_FADER";

/** ステージングしたルーティングを適用できない理由。 */
export type RoutingIssue =
  | "INVALID_ID"
  | "DUPLICATE_ID"
  | "TOO_MANY_ITEMS"
  | "UNKNOWN_NODE"
  | "INVALID_SEND"
  | "INVALID_SEND_TAP"
  | "DELAY_OUT_OF_RANGE"
  | "OUTPUT_CHANNEL_OUT_OF_RANGE"
  | "OVERLAPPING_OUTPUTS"
  | "UNDELAYED_LOOP";

// ---------------------------------------------------------------------------
// 親機モードの状態
// ---------------------------------------------------------------------------

/** 親機で選べる出力デバイス。 */
export interface OutputDevice {
  /** デバイス固有の永続 ID（エンドポイント ID）。 */
  id: string;
  name: string;
}

/** 親機のセッション全体の状態。ステータス帯に表示する。 */
export interface SessionInfo {
  entity_id: string;
  revision: number;
  name: string;
  state: SessionState;
  /** ERROR のときの原因コード。 */
  error_code: ErrorCode | null;
  /** 招待 URL。READY と ACTIVE のときだけ値を持つ。 */
  invite_url: string | null;
  output_device_state: OutputDeviceState;
  /** OPEN のときのデバイス名。 */
  output_device_name: string | null;
  /** 選択中の出力デバイスの ID。NONE では null。LOST では抜けたデバイスの ID を残す。 */
  output_device_id: string | null;
  /** 選べる出力デバイス。UI の接続時と、選択や抜去のときにエンジンが列挙し直す。 */
  output_devices: OutputDevice[];
  /** 利用可能な更新のバージョン。なければ null。 */
  update_available: string | null;
}

/** 入力チャンネルストリップ 1 本の状態。並び順はエンジンが決め、UI は並べ替えない。 */
export interface ChannelStrip {
  entity_id: string;
  revision: number;
  participant_id: string;
  name: string;
  /** 参加者を見分けるための短い識別 ID。 */
  short_id: string;
  connection_state: Exclude<ConnectionState, "PENDING">;
  /** connection_state が CONNECTED のときだけ値を持つ。 */
  signal_state: SignalState | null;
  /** 子機側の送信ミュート。親機からは解除できない。 */
  send_muted: boolean;
  /** 親機側のチャンネルミュート。 */
  channel_muted: boolean;
  gain_db: GainDb;
  /** FAILED のときの原因コード。 */
  error_code: ErrorCode | null;
}

/** Master ストリップの状態。 */
export interface MasterStrip {
  entity_id: string;
  revision: number;
  gain_db: GainDb;
  master_muted: boolean;
  /** master_muted が true のときの理由。 */
  master_mute_reason: MasterMuteReason | null;
}

/** 承認待ちの参加者。 */
export interface PendingParticipant {
  entity_id: string;
  revision: number;
  name: string;
  short_id: string;
}

/** ルーティングのバス。entity_id は "bus-" で始める。 */
export interface RoutingBus {
  entity_id: string;
  name: string;
  kind: BusKind;
}

/** 追加の物理出力。メインの出力と同じデバイスの、device_channel から 2 チャンネルに書く。entity_id は "out-" で始める。 */
export interface RoutingOutput {
  entity_id: string;
  name: string;
  device_channel: number;
}

/**
 * センド（ノードからノードへの結線）。entity_id は "send-" で始める。
 * ストリップから Master と、Master からメインの出力への結線は暗黙にあるので書かない。
 */
export interface RoutingSend {
  entity_id: string;
  from: string;
  to: string;
  tap: SendTap;
}

/** フィードバック経路の遅延ノード。entity_id は "delay-" で始める。 */
export interface RoutingDelay {
  entity_id: string;
  delay_ms: number;
}

/** 入力ストリップの手動ディレイ。 */
export interface InputDelay {
  strip_id: string;
  delay_ms: number;
}

/** ルーティングの構成。UI がステージングし、エンジンが検証して適用する。 */
export interface RoutingConfig {
  buses: RoutingBus[];
  outputs: RoutingOutput[];
  sends: RoutingSend[];
  delays: RoutingDelay[];
  input_delays: InputDelay[];
}

/** ステージングした構成の検証の結果。 */
export interface RoutingCheck {
  /** 適用できない理由。null なら適用できる（needs_approval なら承認が要る）。 */
  issue: RoutingIssue | null;
  issue_entity_id: string | null;
  /** ループに含まれるノード。ルーティング画面で強調する。 */
  looped_entity_ids: string[];
  /** 遅延ノードを通るループがあり、適用にはオペレーターの承認が要る。 */
  needs_approval: boolean;
}

/** ステージング中の構成と検証の結果。 */
export interface StagedRouting {
  config: RoutingConfig;
  check: RoutingCheck;
}

/** バスかセンドのフェーダーとミュート。新しいバスとセンドはミュートで作られる。 */
export interface RouteLevel {
  entity_id: string;
  revision: number;
  gain_db: GainDb;
  muted: boolean;
}

/** ルーティング全体の状態。entity_id は "routing"。 */
export interface RoutingInfo {
  entity_id: string;
  revision: number;
  applied: RoutingConfig;
  /** 適用中の構成のループを、オペレーターが承認して適用した。 */
  feedback_approved: boolean;
  staged: StagedRouting | null;
  levels: RouteLevel[];
}

/** 親機モードの全状態。接続時にエンジンから届く。 */
export interface MixerSnapshot {
  session: SessionInfo;
  channels: ChannelStrip[];
  master: MasterStrip;
  pending: PendingParticipant[];
  routing: RoutingInfo;
}

// ---------------------------------------------------------------------------
// 子機モードの状態
// ---------------------------------------------------------------------------

/** 子機で選べる入力デバイス。id はデバイス固有の永続 ID。 */
export interface InputDevice {
  id: string;
  name: string;
}

/** 子機モードの全状態。 */
export interface ClientSnapshot {
  entity_id: string;
  revision: number;
  client_state: ClientState;
  /** FAILED の原因、または DISCONNECTED の直前の終了理由。 */
  failure_code: ErrorCode | null;
  /** 接続先のセッション名。わかるまでは null。 */
  session_name: string | null;
  input_devices: InputDevice[];
  /** 選択中の入力デバイス。null は「入力なし」。 */
  selected_input_id: string | null;
  /** 選択中のデバイスが抜去されて入力がない状態。 */
  input_missing: boolean;
  send_muted: boolean;
  /** 送信レベル（-60〜+12 dB）。 */
  send_level_db: number;
}

// ---------------------------------------------------------------------------
// UI からエンジンへのコマンド
// ---------------------------------------------------------------------------

/** 離散コマンドの共通部分。エンジンは適用後に applied_revision を ACK で返す。 */
interface DiscreteCommandBase {
  request_id: string;
  entity_id: string;
  /** UI が表示している entity の revision。古ければエンジンが REVISION_CONFLICT で拒否する。 */
  expected_revision: number;
}

/** 離散コマンドの本体（request_id と revision を除いた部分）。 */
export type DiscreteCommandBody =
  | { type: "create_session" }
  | { type: "end_session" }
  | { type: "set_channel_mute"; muted: boolean }
  | { type: "set_master_mute"; muted: boolean }
  | { type: "approve_participant" }
  | { type: "reject_participant" }
  | { type: "remove_strip" }
  | { type: "join"; invite: string }
  | { type: "leave" }
  | { type: "select_input"; device_id: string | null }
  | { type: "set_send_mute"; muted: boolean }
  | { type: "stage_routing"; config: RoutingConfig }
  | { type: "commit_routing"; approve_feedback: boolean }
  | { type: "discard_routing" }
  | { type: "set_route_mute"; muted: boolean }
  | { type: "select_output"; device_id: string | null };

/** 離散コマンド。 */
export type DiscreteCommand = DiscreteCommandBase & DiscreteCommandBody;

/**
 * 連続値のコマンド。フェーダーや送信レベルなどに使う。
 * entity ごとのシーケンス番号で後勝ちにし、エンジンは gain_ack で適用を知らせる。
 */
export interface SetGainCommand {
  type: "set_gain";
  entity_id: string;
  seq: number;
  gain_db: GainDb;
}

/** UI からエンジンへのメッセージ。 */
export type UiToEngineMessage = DiscreteCommand | SetGainCommand | { type: "hello" };

// ---------------------------------------------------------------------------
// エンジンから UI へのメッセージ
// ---------------------------------------------------------------------------

/** 離散コマンドの適用完了。 */
export interface AckMessage {
  type: "ack";
  request_id: string;
  applied_revision: number;
}

/** 離散コマンドの拒否。 */
export interface RejectMessage {
  type: "reject";
  request_id: string;
  error_code: ErrorCode;
}

/** 連続値コマンドの適用完了。seq 以下のコマンドがすべて適用済みであることを表す。 */
export interface GainAckMessage {
  type: "gain_ack";
  entity_id: string;
  seq: number;
  applied_revision: number;
}

/**
 * メーターの統計値。エンジンが 10〜30 Hz で送る。
 * levels は entity_id ごとのプリフェーダーのピーク（dBFS、null は無音）。
 */
export interface MetersMessage {
  type: "meters";
  levels: Record<string, number | null>;
}

/** エンジンからの通知。UI はトーストで表示する。 */
export interface NoticeMessage {
  type: "notice";
  severity: "info" | "warning";
  /** 通知の種別。i18n のキー（notice.<code>）に対応する。 */
  code: string;
  /** 文言に埋め込む値（参加者名など）。 */
  params: Record<string, string>;
}

/** エンジンから UI へのメッセージ。 */
export type EngineToUiMessage =
  | { type: "mixer_snapshot"; snapshot: MixerSnapshot }
  | { type: "client_snapshot"; snapshot: ClientSnapshot }
  | { type: "session_updated"; session: SessionInfo }
  | { type: "channel_updated"; channel: ChannelStrip }
  | { type: "channel_removed"; entity_id: string }
  | { type: "master_updated"; master: MasterStrip }
  | { type: "pending_updated"; pending: PendingParticipant[] }
  | { type: "client_updated"; client: ClientSnapshot }
  | { type: "routing_updated"; routing: RoutingInfo }
  | { type: "route_level_updated"; level: RouteLevel }
  | AckMessage
  | RejectMessage
  | GainAckMessage
  | MetersMessage
  | NoticeMessage;
