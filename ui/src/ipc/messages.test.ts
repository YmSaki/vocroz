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
  InputDevice,
  MasterMuteReason,
  MasterStrip,
  MixerSnapshot,
  OutputDeviceState,
  PendingParticipant,
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
  set_send_mute: keysOf<UiMessageOf<"set_send_mute">>({ ...kDiscreteKeys, muted: true }),
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
  ack: keysOf<EngineMessageOf<"ack">>({ type: true, request_id: true, applied_revision: true }),
  reject: keysOf<EngineMessageOf<"reject">>({ type: true, request_id: true, error_code: true }),
  gain_ack: keysOf<EngineMessageOf<"gain_ack">>({
    type: true,
    entity_id: true,
    seq: true,
    applied_revision: true,
  }),
  meters: keysOf<EngineMessageOf<"meters">>({ type: true, levels: true }),
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
  update_available: true,
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
}

function checkMaster(master: MasterStrip, seen: Seen): void {
  expectKeys(master, kMasterKeys);
  seen.add(kMasterMuteReasons, master.master_mute_reason);
}

function checkPending(pending: PendingParticipant[]): void {
  for (const participant of pending) expectKeys(participant, kPendingKeys);
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
    case "reject":
      seen.add(kErrorCodes, message.error_code);
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
