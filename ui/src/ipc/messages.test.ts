// SPDX-License-Identifier: AGPL-3.0-only
// messages.ts の型が、スキーマの正本（modules/control-protocol）と同じ形を表していることを確かめる。
//
// C++ のテストと同じ全メッセージの JSON の例（modules/control-protocol/tests/data/）を読み、
// 種別、フィールド名、状態の正規名がこのファイルの型の定義と一致することを実行時に確かめる。
// 型の側の一覧は Record<keyof T, true> で書くので、型にフィールドを足したのに一覧を直し忘れると型検査で止まる
// （docs/adr/0015-control-protocol-messages.md）。

import { describe, expect, it } from "vitest";
import engineToUiExamples from "../../../modules/control-protocol/tests/data/engine_to_ui.json";
import uiToEngineExamples from "../../../modules/control-protocol/tests/data/ui_to_engine.json";
import { ClientStore } from "../state/client-store";
import { MixerStore } from "../state/mixer-store";
import type {
  ChannelStrip,
  ClientSnapshot,
  ClientState,
  ConnectionState,
  EngineToUiMessage,
  ErrorCode,
  InputDelay,
  InputDevice,
  LinkStat,
  MasterMuteReason,
  MasterStrip,
  BusKind,
  InputSourceKind,
  MixerSnapshot,
  OutputDeviceState,
  PendingParticipant,
  RouteLevel,
  RoutingBus,
  RoutingCheck,
  RoutingConfig,
  RoutingDelay,
  RoutingInfo,
  RoutingIssue,
  RoutingOutput,
  RoutingSend,
  SendTap,
  StagedRouting,
  SessionInfo,
  SessionState,
  SignalState,
  UiToEngineMessage,
} from "./messages";

/** T のすべてのキー。過不足があれば型検査で止まる。 */
function keysOf<T>(keys: Record<keyof T, true>): string[] {
  return Object.keys(keys).sort();
}

type EngineMessageOf<K extends EngineToUiMessage["type"]> = Extract<EngineToUiMessage, { type: K }>;
type UiMessageOf<K extends UiToEngineMessage["type"]> = Extract<UiToEngineMessage, { type: K }>;

const kDiscreteKeys = {
  type: true,
  request_id: true,
  entity_id: true,
  expected_revision: true,
} as const;

/** UI からエンジンへの種別ごとのフィールド。 */
const kUiMessageKeys: { [K in UiToEngineMessage["type"]]: string[] } = {
  hello: keysOf<UiMessageOf<"hello">>({ type: true }),
  set_gain: keysOf<UiMessageOf<"set_gain">>({
    type: true,
    entity_id: true,
    seq: true,
    gain_db: true,
  }),
  create_session: keysOf<UiMessageOf<"create_session">>(kDiscreteKeys),
  end_session: keysOf<UiMessageOf<"end_session">>(kDiscreteKeys),
  set_channel_mute: keysOf<UiMessageOf<"set_channel_mute">>({ ...kDiscreteKeys, muted: true }),
  set_master_mute: keysOf<UiMessageOf<"set_master_mute">>({ ...kDiscreteKeys, muted: true }),
  approve_participant: keysOf<UiMessageOf<"approve_participant">>(kDiscreteKeys),
  reject_participant: keysOf<UiMessageOf<"reject_participant">>(kDiscreteKeys),
  remove_strip: keysOf<UiMessageOf<"remove_strip">>(kDiscreteKeys),
  join: keysOf<UiMessageOf<"join">>({ ...kDiscreteKeys, invite: true }),
  leave: keysOf<UiMessageOf<"leave">>(kDiscreteKeys),
  select_input: keysOf<UiMessageOf<"select_input">>({ ...kDiscreteKeys, device_id: true }),
  select_output: keysOf<UiMessageOf<"select_output">>({ ...kDiscreteKeys, device_id: true }),
  set_send_mute: keysOf<UiMessageOf<"set_send_mute">>({ ...kDiscreteKeys, muted: true }),
  stage_routing: keysOf<UiMessageOf<"stage_routing">>({ ...kDiscreteKeys, config: true }),
  commit_routing: keysOf<UiMessageOf<"commit_routing">>({
    ...kDiscreteKeys,
    approve_feedback: true,
  }),
  discard_routing: keysOf<UiMessageOf<"discard_routing">>(kDiscreteKeys),
  set_route_mute: keysOf<UiMessageOf<"set_route_mute">>({ ...kDiscreteKeys, muted: true }),
  set_latency_target: keysOf<UiMessageOf<"set_latency_target">>({
    ...kDiscreteKeys,
    target_ms: true,
  }),
};

/** エンジンから UI への種別ごとのフィールド。 */
const kEngineMessageKeys: { [K in EngineToUiMessage["type"]]: string[] } = {
  mixer_snapshot: keysOf<EngineMessageOf<"mixer_snapshot">>({ type: true, snapshot: true }),
  client_snapshot: keysOf<EngineMessageOf<"client_snapshot">>({ type: true, snapshot: true }),
  session_updated: keysOf<EngineMessageOf<"session_updated">>({ type: true, session: true }),
  channel_updated: keysOf<EngineMessageOf<"channel_updated">>({ type: true, channel: true }),
  channel_removed: keysOf<EngineMessageOf<"channel_removed">>({ type: true, entity_id: true }),
  master_updated: keysOf<EngineMessageOf<"master_updated">>({ type: true, master: true }),
  pending_updated: keysOf<EngineMessageOf<"pending_updated">>({ type: true, pending: true }),
  client_updated: keysOf<EngineMessageOf<"client_updated">>({ type: true, client: true }),
  routing_updated: keysOf<EngineMessageOf<"routing_updated">>({ type: true, routing: true }),
  route_level_updated: keysOf<EngineMessageOf<"route_level_updated">>({ type: true, level: true }),
  ack: keysOf<EngineMessageOf<"ack">>({ type: true, request_id: true, applied_revision: true }),
  reject: keysOf<EngineMessageOf<"reject">>({ type: true, request_id: true, error_code: true }),
  gain_ack: keysOf<EngineMessageOf<"gain_ack">>({
    type: true,
    entity_id: true,
    seq: true,
    applied_revision: true,
  }),
  meters: keysOf<EngineMessageOf<"meters">>({ type: true, levels: true }),
  link_stats: keysOf<EngineMessageOf<"link_stats">>({ type: true, stats: true }),
  notice: keysOf<EngineMessageOf<"notice">>({
    type: true,
    severity: true,
    code: true,
    params: true,
  }),
};

const kSessionKeys = keysOf<SessionInfo>({
  entity_id: true,
  revision: true,
  name: true,
  state: true,
  error_code: true,
  invite_url: true,
  output_device_state: true,
  output_device_name: true,
  output_device_id: true,
  output_devices: true,
  update_available: true,
  latency_target_ms: true,
});
const kChannelKeys = keysOf<ChannelStrip>({
  entity_id: true,
  revision: true,
  participant_id: true,
  name: true,
  short_id: true,
  connection_state: true,
  signal_state: true,
  send_muted: true,
  channel_muted: true,
  gain_db: true,
  error_code: true,
  source_kind: true,
  channel_count: true,
  source_number: true,
});
const kMasterKeys = keysOf<MasterStrip>({
  entity_id: true,
  revision: true,
  gain_db: true,
  master_muted: true,
  master_mute_reason: true,
});
const kPendingKeys = keysOf<PendingParticipant>({
  entity_id: true,
  revision: true,
  name: true,
  short_id: true,
});
const kMixerSnapshotKeys = keysOf<MixerSnapshot>({
  session: true,
  channels: true,
  master: true,
  pending: true,
  routing: true,
});
const kRoutingKeys = keysOf<RoutingInfo>({
  entity_id: true,
  revision: true,
  applied: true,
  feedback_approved: true,
  staged: true,
  levels: true,
});
const kRoutingConfigKeys = keysOf<RoutingConfig>({
  buses: true,
  outputs: true,
  sends: true,
  delays: true,
  input_delays: true,
});
const kRoutingBusKeys = keysOf<RoutingBus>({ entity_id: true, name: true, kind: true });
const kRoutingOutputKeys = keysOf<RoutingOutput>({
  entity_id: true,
  name: true,
  device_channel: true,
});
const kRoutingSendKeys = keysOf<RoutingSend>({ entity_id: true, from: true, to: true, tap: true });
const kRoutingDelayKeys = keysOf<RoutingDelay>({ entity_id: true, delay_ms: true });
const kInputDelayKeys = keysOf<InputDelay>({ strip_id: true, delay_ms: true });
const kStagedKeys = keysOf<StagedRouting>({ config: true, check: true });
const kCheckKeys = keysOf<RoutingCheck>({
  issue: true,
  issue_entity_id: true,
  looped_entity_ids: true,
  needs_approval: true,
});
const kRouteLevelKeys = keysOf<RouteLevel>({
  entity_id: true,
  revision: true,
  gain_db: true,
  muted: true,
});
const kClientKeys = keysOf<ClientSnapshot>({
  entity_id: true,
  revision: true,
  client_state: true,
  failure_code: true,
  session_name: true,
  input_devices: true,
  selected_input_id: true,
  input_missing: true,
  send_muted: true,
  send_level_db: true,
});
const kInputDeviceKeys = keysOf<InputDevice>({ id: true, name: true });
const kLinkStatKeys = keysOf<LinkStat>({ jitter_ms: true, buffer_ms: true });

// 状態の正規名。Record<型, true> なので、型に値を足したのに一覧を直し忘れると型検査で止まる。
const kSessionStates: Record<SessionState, true> = {
  IDLE: true,
  CREATING: true,
  READY: true,
  ACTIVE: true,
  ENDING: true,
  ERROR: true,
};
const kConnectionStates: Record<Exclude<ConnectionState, "PENDING">, true> = {
  CONNECTING: true,
  CONNECTED: true,
  RECONNECTING: true,
  LOST: true,
  FAILED: true,
};
const kSignalStates: Record<SignalState, true> = {
  RECEIVING: true,
  PAUSED: true,
  RESUMING: true,
  NO_SIGNAL: true,
};
const kOutputDeviceStates: Record<OutputDeviceState, true> = { NONE: true, OPEN: true, LOST: true };
const kMasterMuteReasons: Record<MasterMuteReason, true> = { OPERATOR: true, SAFETY: true };
const kClientStates: Record<ClientState, true> = {
  DISCONNECTED: true,
  RESOLVING: true,
  AUTHENTICATING: true,
  WAITING_APPROVAL: true,
  CONNECTING: true,
  CONNECTED: true,
  RECONNECTING: true,
  FAILED: true,
};
const kErrorCodes: Record<ErrorCode, true> = {
  REJECTED: true,
  INVITE_EXPIRED: true,
  INVITE_REVOKED: true,
  INVITE_INVALID: true,
  UNREACHABLE: true,
  RECONNECT_TIMEOUT: true,
  SESSION_ENDED: true,
  REVISION_CONFLICT: true,
  NOT_FOUND: true,
  INVALID_STATE: true,
  ENGINE_ERROR: true,
  DEVICE_UNAVAILABLE: true,
};

const kBusKinds: Record<BusKind, true> = { USER: true, CUE: true };
const kInputSourceKinds: Record<InputSourceKind, true> = { MICROPHONE: true, APP_AUDIO: true };
const kSendTaps: Record<SendTap, true> = { POST_FADER: true, PRE_FADER: true };
const kRoutingIssues: Record<RoutingIssue, true> = {
  INVALID_ID: true,
  DUPLICATE_ID: true,
  TOO_MANY_ITEMS: true,
  UNKNOWN_NODE: true,
  INVALID_SEND: true,
  INVALID_SEND_TAP: true,
  DELAY_OUT_OF_RANGE: true,
  OUTPUT_CHANNEL_OUT_OF_RANGE: true,
  OVERLAPPING_OUTPUTS: true,
  UNDELAYED_LOOP: true,
};

/** JSON の例に現れた状態の値を集める。 */
class Seen {
  private readonly values = new Map<object, Set<string>>();

  add(table: object, value: string | null): void {
    if (value === null) return;
    expect(Object.keys(table)).toContain(value);
    const set = this.values.get(table) ?? new Set<string>();
    set.add(value);
    this.values.set(table, set);
  }

  /** 一覧の値がすべて現れたか。 */
  coversAll(table: object): boolean {
    return (this.values.get(table)?.size ?? 0) === Object.keys(table).length;
  }
}

function expectKeys(value: object, keys: string[]): void {
  expect(Object.keys(value).sort()).toEqual(keys);
}

function checkSession(session: SessionInfo, seen: Seen): void {
  expectKeys(session, kSessionKeys);
  seen.add(kSessionStates, session.state);
  seen.add(kErrorCodes, session.error_code);
  seen.add(kOutputDeviceStates, session.output_device_state);
}

function checkChannel(channel: ChannelStrip, seen: Seen): void {
  expectKeys(channel, kChannelKeys);
  seen.add(kConnectionStates, channel.connection_state);
  seen.add(kSignalStates, channel.signal_state);
  seen.add(kErrorCodes, channel.error_code);
  seen.add(kInputSourceKinds, channel.source_kind);
}

function checkMaster(master: MasterStrip, seen: Seen): void {
  expectKeys(master, kMasterKeys);
  seen.add(kMasterMuteReasons, master.master_mute_reason);
}

function checkPending(pending: PendingParticipant[]): void {
  for (const participant of pending) expectKeys(participant, kPendingKeys);
}

function checkRoutingConfig(config: RoutingConfig, seen: Seen): void {
  expectKeys(config, kRoutingConfigKeys);
  for (const bus of config.buses) {
    expectKeys(bus, kRoutingBusKeys);
    seen.add(kBusKinds, bus.kind);
  }
  for (const output of config.outputs) expectKeys(output, kRoutingOutputKeys);
  for (const send of config.sends) {
    expectKeys(send, kRoutingSendKeys);
    seen.add(kSendTaps, send.tap);
  }
  for (const delay of config.delays) expectKeys(delay, kRoutingDelayKeys);
  for (const delay of config.input_delays) expectKeys(delay, kInputDelayKeys);
}

function checkRouteLevel(level: RouteLevel): void {
  expectKeys(level, kRouteLevelKeys);
}

function checkRouting(routing: RoutingInfo, seen: Seen): void {
  expectKeys(routing, kRoutingKeys);
  checkRoutingConfig(routing.applied, seen);
  if (routing.staged !== null) {
    expectKeys(routing.staged, kStagedKeys);
    checkRoutingConfig(routing.staged.config, seen);
    expectKeys(routing.staged.check, kCheckKeys);
    seen.add(kRoutingIssues, routing.staged.check.issue);
  }
  for (const level of routing.levels) checkRouteLevel(level);
}

function checkClient(client: ClientSnapshot, seen: Seen): void {
  expectKeys(client, kClientKeys);
  for (const device of client.input_devices) expectKeys(device, kInputDeviceKeys);
  seen.add(kClientStates, client.client_state);
  seen.add(kErrorCodes, client.failure_code);
}

function checkEngineMessage(message: EngineToUiMessage, seen: Seen): void {
  expectKeys(message, kEngineMessageKeys[message.type]);
  switch (message.type) {
    case "mixer_snapshot":
      expectKeys(message.snapshot, kMixerSnapshotKeys);
      checkSession(message.snapshot.session, seen);
      for (const channel of message.snapshot.channels) checkChannel(channel, seen);
      checkMaster(message.snapshot.master, seen);
      checkPending(message.snapshot.pending);
      checkRouting(message.snapshot.routing, seen);
      return;
    case "client_snapshot":
      checkClient(message.snapshot, seen);
      return;
    case "session_updated":
      checkSession(message.session, seen);
      return;
    case "channel_updated":
      checkChannel(message.channel, seen);
      return;
    case "master_updated":
      checkMaster(message.master, seen);
      return;
    case "pending_updated":
      checkPending(message.pending);
      return;
    case "client_updated":
      checkClient(message.client, seen);
      return;
    case "routing_updated":
      checkRouting(message.routing, seen);
      return;
    case "route_level_updated":
      checkRouteLevel(message.level);
      return;
    case "reject":
      seen.add(kErrorCodes, message.error_code);
      return;
    case "link_stats":
      for (const stat of Object.values(message.stats)) expectKeys(stat, kLinkStatKeys);
      return;
    case "channel_removed":
    case "ack":
    case "gain_ack":
    case "meters":
    case "notice":
      return;
  }
}

// JSON の例は型の検査を通らない（文字列が string に広がる）ので、実行時に確かめる前提で型を付け直す。
const kUiExamples = uiToEngineExamples as unknown as UiToEngineMessage[];
const kEngineExamples = engineToUiExamples as unknown as EngineToUiMessage[];

describe("messages", () => {
  it("UI からエンジンへの例が、すべての種別を型と同じフィールドで含む", () => {
    const types = new Set<string>();
    for (const message of kUiExamples) {
      expect(Object.keys(kUiMessageKeys)).toContain(message.type);
      expectKeys(message, kUiMessageKeys[message.type]);
      types.add(message.type);
    }
    expect([...types].sort()).toEqual(Object.keys(kUiMessageKeys).sort());
  });

  it("エンジンから UI への例が、すべての種別と正規名を型と同じフィールドで含む", () => {
    const types = new Set<string>();
    const seen = new Seen();
    for (const message of kEngineExamples) {
      expect(Object.keys(kEngineMessageKeys)).toContain(message.type);
      checkEngineMessage(message, seen);
      types.add(message.type);
    }
    expect([...types].sort()).toEqual(Object.keys(kEngineMessageKeys).sort());
    for (const table of [
      kSessionStates,
      kConnectionStates,
      kSignalStates,
      kOutputDeviceStates,
      kMasterMuteReasons,
      kClientStates,
      kErrorCodes,
      kBusKinds,
      kSendTaps,
      kRoutingIssues,
      kInputSourceKinds,
    ]) {
      expect(seen.coversAll(table)).toBe(true);
    }
  });

  it("エンジンから UI への例をストアにそのまま取り込める", () => {
    const mixer = new MixerStore();
    const client = new ClientStore();
    for (const message of kEngineExamples) {
      mixer.apply(message);
      client.apply(message);
    }
    // 例の最後の mixer_snapshot（IDLE）のあとに、差分イベントで ch1 が足される。
    expect(mixer.get()?.channels.map((c) => c.entity_id)).toEqual(["ch1"]);
    expect(mixer.get()?.pending).toEqual([]);
    // 子機の状態は最後の client_updated（FAILED、REJECTED）になる。
    expect(client.get()?.client_state).toBe("FAILED");
    expect(client.get()?.failure_code).toBe("REJECTED");
  });
});
