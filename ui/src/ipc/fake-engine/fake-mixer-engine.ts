// SPDX-License-Identifier: AGPL-3.0-only
// 親機モードのフェイクエンジン。pa-engine の代わりに、ミキサー画面の操作を受けて状態を返す。
// 状態遷移は本物のエンジンを簡略化したもので、画面の開発と手動の確認に使う。

import type {
  ChannelStrip,
  ConnectionState,
  DiscreteCommand,
  MasterStrip,
  MixerSnapshot,
  OutputDevice,
  PendingParticipant,
  RouteLevel,
  SessionInfo,
  SetGainCommand,
  SignalState,
  UiToEngineMessage,
} from "../messages";
import { FakeEngineBase, type FakeEngineOptions } from "./fake-engine-base";
import { FakeRouting } from "./fake-routing";

/** フェイクエンジンの初期状態の種類。demo は画面を一通り確かめられる参加者をあらかじめ置く。 */
export type MixerScenario = "demo" | "empty";

/** 親機モードのフェイクエンジンの設定。 */
export interface FakeMixerEngineOptions extends FakeEngineOptions {
  scenario?: MixerScenario;
}

const kSessionId = "session";
const kMasterId = "master";
const kRoutingId = "routing";
/** 招待 URL の例。HTTPS の /join#<コード> の形（docs/adr/0006-invite-url-scheme.md）。 */
const kInviteUrl = "https://vocroz.example/join#7KQ2-M9XD";

/** 状態遷移の待ち時間（ms）。画面で遷移を目で追える長さにする。 */
const kCreateDelayMs = 400;
const kConnectDelayMs = 800;
const kUnmuteDelayMs = 2500;
const kResumeDelayMs = 200;
const kJoinRequestDelayMs = 2000;

function channel(
  index: number,
  name: string,
  connection: Exclude<ConnectionState, "PENDING">,
  signal: SignalState | null,
  overrides: Partial<ChannelStrip> = {},
): ChannelStrip {
  return {
    entity_id: `ch${index}`,
    revision: 1,
    participant_id: `p${index}`,
    name,
    short_id: `#${String(index).padStart(2, "0")}`,
    connection_state: connection,
    signal_state: connection === "CONNECTED" ? signal : null,
    // 新しい参加者のチャンネルは必ずミュートから始める（爆音防止）。
    send_muted: true,
    channel_muted: true,
    gain_db: 0,
    error_code: null,
    ...overrides,
  };
}

// 選べる出力デバイス。"broken" は開けないデバイスとして DEVICE_UNAVAILABLE を再現する。
const kDefaultOutput: OutputDevice = { id: "usb", name: "スピーカー (USB Audio Interface)" };
const kOutputDevices: OutputDevice[] = [
  kDefaultOutput,
  { id: "onboard", name: "スピーカー (Realtek)" },
  { id: "broken", name: "HDMI (開けないデバイス)" },
];

function initialSnapshot(scenario: MixerScenario): Omit<MixerSnapshot, "routing"> {
  const master: MasterStrip = {
    entity_id: kMasterId,
    revision: 1,
    gain_db: 0,
    // 起動時は Master もミュートから始める。
    master_muted: true,
    master_mute_reason: "OPERATOR",
  };
  const session: SessionInfo = {
    entity_id: kSessionId,
    revision: 1,
    name: "",
    state: "IDLE",
    error_code: null,
    invite_url: null,
    output_device_state: "OPEN",
    output_device_name: kDefaultOutput.name,
    output_device_id: kDefaultOutput.id,
    output_devices: kOutputDevices,
    update_available: null,
  };
  if (scenario === "empty") return { session, channels: [], master, pending: [] };
  return {
    session: {
      ...session,
      name: "日曜リハーサル",
      state: "ACTIVE",
      invite_url: kInviteUrl,
      update_available: "1.0.1",
    },
    channels: [
      channel(1, "Vo 山田", "CONNECTED", "RECEIVING", { send_muted: false }),
      channel(2, "Gt 佐藤", "CONNECTED", "PAUSED"),
      channel(3, "Ba 鈴木", "RECONNECTING", null, { send_muted: false }),
      channel(4, "Dr 田中", "CONNECTED", "NO_SIGNAL", { send_muted: false }),
      channel(5, "Key 高橋", "LOST", null),
      channel(6, "Cho 伊藤", "CONNECTED", "RECEIVING", { send_muted: false, gain_db: null }),
      channel(7, "Sax 中村", "FAILED", null, { error_code: "UNREACHABLE" }),
    ],
    master,
    pending: [{ entity_id: "pending1", revision: 1, name: "Tp 渡辺", short_id: "#08" }],
  };
}

/** 親機モードのフェイクエンジン。 */
export class FakeMixerEngine extends FakeEngineBase {
  private state: Omit<MixerSnapshot, "routing">;
  /** ルーティングは空（バスも追加の出力もない）から始める。 */
  private readonly routing = new FakeRouting(kRoutingId);
  private nextIndex: number;

  constructor(options: FakeMixerEngineOptions = {}) {
    super(options);
    this.state = initialSnapshot(options.scenario ?? "demo");
    this.nextIndex = 9;
  }

  /** 現在の状態（テストでの確認用）。 */
  snapshot(): MixerSnapshot {
    return { ...this.state, routing: this.routing.info() };
  }

  /** 出力デバイスの抜去を再現する。エンジンは別のデバイスに切り替えず、Master を安全ミュートにする。 */
  simulateOutputLost(): void {
    // 出力デバイスの状態は観測値なので revision を進めない。SAFETY のミュートはコマンドで変えられる値を
    // エンジンが変えるので revision を進め、ミュートを知らないまま押した解除を REVISION_CONFLICT にする。
    this.updateSession({ output_device_state: "LOST", output_device_name: null }, false);
    this.updateMaster({ master_muted: true, master_mute_reason: "SAFETY" });
    this.emit({ type: "notice", severity: "warning", code: "output_lost", params: {} });
  }

  /** 参加の申請を再現する。 */
  simulateJoinRequest(name: string): void {
    const index = this.nextIndex++;
    const participant: PendingParticipant = {
      entity_id: `pending${index}`,
      revision: 1,
      name,
      short_id: `#${String(index).padStart(2, "0")}`,
    };
    this.state = { ...this.state, pending: [...this.state.pending, participant] };
    this.emit({ type: "pending_updated", pending: this.state.pending });
    this.emit({ type: "notice", severity: "info", code: "join_request", params: { name } });
  }

  /**
   * 参加者の接続状態の変化を再現する。
   * @param entityId チャンネル
   * @param connection 新しい接続状態
   * @param signal 新しい信号状態（CONNECTED のときだけ）
   */
  simulateConnection(
    entityId: string,
    connection: Exclude<ConnectionState, "PENDING">,
    signal: SignalState | null = null,
  ): void {
    // 接続状態と信号状態は観測値なので revision を進めない（その最中のミュートを衝突させないため）。
    this.updateChannel(
      entityId,
      {
        connection_state: connection,
        signal_state: connection === "CONNECTED" ? signal : null,
      },
      false,
    );
  }

  protected receive(message: UiToEngineMessage): void {
    switch (message.type) {
      case "hello":
        this.emit({ type: "mixer_snapshot", snapshot: this.snapshot() });
        return;
      case "set_gain":
        this.applyGain(message);
        return;
      case "create_session":
      case "end_session":
      case "set_channel_mute":
      case "set_master_mute":
      case "approve_participant":
      case "reject_participant":
      case "remove_strip":
      case "select_output":
      case "stage_routing":
      case "commit_routing":
      case "discard_routing":
      case "set_route_mute":
        this.applyCommand(message);
        return;
      case "join":
      case "leave":
      case "select_input":
      case "set_send_mute":
        this.reject(message, "INVALID_STATE");
        return;
    }
  }

  protected meterLevels(): Record<string, number | null> {
    const levels: Record<string, number | null> = {};
    let masterLevel: number | null = null;
    for (const c of this.state.channels) {
      // プリフェーダーのメーター。親機のチャンネルミュート中もデータは届くので、入力を監視できる。
      const receiving = c.connection_state === "CONNECTED" && c.signal_state === "RECEIVING";
      const level = receiving ? this.signalLevel(-18) : null;
      levels[c.entity_id] = level;
      if (level !== null && !c.channel_muted && c.gain_db !== null) {
        const post = level + c.gain_db;
        masterLevel = masterLevel === null ? post : Math.max(masterLevel, post);
      }
    }
    const master = this.state.master;
    levels[master.entity_id] =
      master.master_muted || masterLevel === null || master.gain_db === null
        ? null
        : Math.min(0, masterLevel + master.gain_db);
    return levels;
  }

  private applyGain(message: SetGainCommand): void {
    if (this.routing.level(message.entity_id) !== undefined) {
      this.applyRouteGain(message);
      return;
    }
    const target =
      message.entity_id === kMasterId
        ? this.state.master
        : this.state.channels.find((c) => c.entity_id === message.entity_id);
    if (target === undefined) return;
    // 連続値は seq の後勝ちで、revision を進めない（離散操作の expected_revision と衝突させないため）。
    if (message.entity_id === kMasterId) this.updateMaster({ gain_db: message.gain_db }, false);
    else this.updateChannel(message.entity_id, { gain_db: message.gain_db }, false);
    if (this.dropAcks) return;
    this.emit({
      type: "gain_ack",
      entity_id: message.entity_id,
      seq: message.seq,
      applied_revision: target.revision,
    });
  }

  private applyRouteGain(message: SetGainCommand): void {
    const level = this.routing.setGain(message.entity_id, message.gain_db);
    if (level === undefined) return;
    this.emitRouteLevel(level);
    if (this.dropAcks) return;
    this.emit({
      type: "gain_ack",
      entity_id: message.entity_id,
      seq: message.seq,
      applied_revision: level.revision,
    });
  }

  private applyCommand(command: DiscreteCommand): void {
    const revision = this.revisionOf(command.entity_id);
    if (revision === undefined) {
      this.reject(command, "NOT_FOUND");
      return;
    }
    if (revision !== command.expected_revision) {
      this.reject(command, "REVISION_CONFLICT");
      return;
    }
    switch (command.type) {
      case "create_session":
        if (this.state.session.state !== "IDLE") return this.reject(command, "INVALID_STATE");
        this.ack(command, this.updateSession({ state: "CREATING", name: "新しいセッション" }));
        this.later(() => {
          // 作成の完了はエンジンの処理の進行で、コマンドで変えた値ではないので revision を進めない。
          this.updateSession({ state: "READY", invite_url: kInviteUrl }, false);
          this.later(() => this.simulateJoinRequest("Vo 小林"), kJoinRequestDelayMs);
        }, kCreateDelayMs);
        return;
      case "end_session":
        if (this.state.session.state !== "READY" && this.state.session.state !== "ACTIVE") {
          return this.reject(command, "INVALID_STATE");
        }
        this.ack(command, this.updateSession({ state: "ENDING", invite_url: null }));
        this.later(() => {
          const removed = this.state.channels.map((c) => c.entity_id);
          for (const id of removed) this.emit({ type: "channel_removed", entity_id: id });
          this.state = { ...this.state, channels: [], pending: [] };
          // バスや出力の構成はセッションをまたいで残し、消えたストリップへの参照だけを片付ける。
          let routingChanged = false;
          for (const id of removed)
            routingChanged = this.routing.forgetStrip(id, []) || routingChanged;
          if (routingChanged) this.emitRouting();
          this.emit({ type: "pending_updated", pending: [] });
          this.updateMaster({ master_muted: true, master_mute_reason: "OPERATOR" });
          this.updateSession({ state: "IDLE", name: "" }, false);
        }, kCreateDelayMs);
        return;
      case "set_channel_mute":
        this.ack(command, this.updateChannel(command.entity_id, { channel_muted: command.muted }));
        return;
      case "set_master_mute":
        // SAFETY のミュートも、解除はオペレーターの明示的な操作だけで行う。
        this.ack(
          command,
          this.updateMaster({
            master_muted: command.muted,
            master_mute_reason: command.muted ? "OPERATOR" : null,
          }),
        );
        return;
      case "approve_participant":
        this.approve(command);
        return;
      case "select_output":
        this.selectOutput(command, command.device_id);
        return;
      case "reject_participant":
        this.removePending(command.entity_id);
        this.ack(command, revision + 1);
        return;
      case "remove_strip": {
        const strip = this.state.channels.find((c) => c.entity_id === command.entity_id);
        if (strip?.connection_state !== "LOST" && strip?.connection_state !== "FAILED") {
          return this.reject(command, "INVALID_STATE");
        }
        this.state = {
          ...this.state,
          channels: this.state.channels.filter((c) => c.entity_id !== command.entity_id),
        };
        this.ack(command, revision + 1);
        this.emit({ type: "channel_removed", entity_id: command.entity_id });
        if (this.routing.forgetStrip(command.entity_id, this.stripIds())) this.emitRouting();
        this.refreshSessionState();
        return;
      }
      case "stage_routing":
        if (command.entity_id !== kRoutingId) return this.reject(command, "INVALID_STATE");
        // ステージングは検証の結果を示すための段階なので、問題のある構成も受け付ける。
        this.ack(command, this.routing.stage(command.config, this.stripIds()));
        this.emitRouting();
        return;
      case "commit_routing":
      case "discard_routing": {
        if (command.entity_id !== kRoutingId) return this.reject(command, "INVALID_STATE");
        const result =
          command.type === "commit_routing"
            ? this.routing.commit(command.approve_feedback, this.stripIds())
            : this.routing.discard();
        if (!result.ok) {
          this.reject(command, result.error);
          // 検証し直した結果が変わっていることがあるので、理由を画面が示せるよう知らせる。
          this.emitRouting();
          return;
        }
        this.ack(command, result.revision);
        this.emitRouting();
        return;
      }
      case "set_route_mute": {
        const level = this.routing.setMute(command.entity_id, command.muted);
        if (level === undefined) return this.reject(command, "INVALID_STATE");
        this.ack(command, level.revision);
        this.emitRouteLevel(level);
        return;
      }
      case "join":
      case "leave":
      case "select_input":
      case "set_send_mute":
        this.reject(command, "INVALID_STATE");
        return;
    }
  }

  private approve(command: DiscreteCommand): void {
    const participant = this.state.pending.find((p) => p.entity_id === command.entity_id);
    if (participant === undefined) return this.reject(command, "NOT_FOUND");
    this.removePending(participant.entity_id);
    const index = this.nextIndex++;
    const strip = channel(index, participant.name, "CONNECTING", null, {
      short_id: participant.short_id,
    });
    this.state = { ...this.state, channels: [...this.state.channels, strip] };
    this.ack(command, participant.revision + 1);
    this.emit({ type: "channel_updated", channel: strip });
    // 接続が確立したら、子機側が送信ミュートを解除するまでの流れを再現する。
    this.later(() => {
      this.simulateConnection(strip.entity_id, "CONNECTED", "PAUSED");
      this.refreshSessionState();
      this.later(() => {
        // 子機の送信ミュートは親機からは変えられない観測値なので、revision を進めない。
        this.updateChannel(strip.entity_id, { send_muted: false, signal_state: "RESUMING" }, false);
        this.later(
          () => this.updateChannel(strip.entity_id, { signal_state: "RECEIVING" }, false),
          kResumeDelayMs,
        );
      }, kUnmuteDelayMs);
    }, kConnectDelayMs);
  }

  private removePending(entityId: string): void {
    this.state = {
      ...this.state,
      pending: this.state.pending.filter((p) => p.entity_id !== entityId),
    };
    this.emit({ type: "pending_updated", pending: this.state.pending });
  }

  /** 接続中の参加者の有無で READY と ACTIVE を切り替える。 */
  private refreshSessionState(): void {
    const { state } = this.state.session;
    if (state !== "READY" && state !== "ACTIVE") return;
    const active = this.state.channels.some(
      (c) => c.connection_state === "CONNECTED" || c.connection_state === "RECONNECTING",
    );
    const next = active ? "ACTIVE" : "READY";
    if (next !== state) this.updateSession({ state: next }, false);
  }

  private revisionOf(entityId: string): number | undefined {
    if (entityId === kSessionId) return this.state.session.revision;
    if (entityId === kMasterId) return this.state.master.revision;
    if (entityId === kRoutingId) return this.routing.info().revision;
    return (
      this.state.channels.find((c) => c.entity_id === entityId)?.revision ??
      this.state.pending.find((p) => p.entity_id === entityId)?.revision ??
      this.routing.level(entityId)?.revision
    );
  }

  private stripIds(): string[] {
    return this.state.channels.map((c) => c.entity_id);
  }

  private emitRouting(): void {
    this.emit({ type: "routing_updated", routing: this.routing.info() });
  }

  private emitRouteLevel(level: RouteLevel): void {
    this.emit({ type: "route_level_updated", level });
  }

  /**
   * revision は、コマンドで変えられる値が変わったときだけ進める（bumpRevision = true）。
   * 観測値（接続状態、信号状態など）だけの変化では進めない（docs/adr/0015-control-protocol-messages.md）。
   */
  // 出力デバイスの選択。選んでも Master のミュートは変えない。開けなければ前のデバイスも閉じたまま。
  private selectOutput(command: DiscreteCommand, deviceId: string | null): void {
    if (command.entity_id !== kSessionId) return this.reject(command, "INVALID_STATE");
    const session = this.state.session;
    if (session.output_device_state === "OPEN" && session.output_device_id === deviceId) {
      this.ack(command, session.revision);
      return;
    }
    const device = session.output_devices.find((d) => d.id === deviceId);
    if (deviceId !== null && (device === undefined || device.id === "broken")) {
      this.updateSession({
        output_device_state: "NONE",
        output_device_name: null,
        output_device_id: null,
      });
      this.reject(command, "DEVICE_UNAVAILABLE");
      return;
    }
    const revision = this.updateSession(
      device === undefined
        ? { output_device_state: "NONE", output_device_name: null, output_device_id: null }
        : {
            output_device_state: "OPEN",
            output_device_name: device.name,
            output_device_id: device.id,
          },
    );
    this.ack(command, revision);
  }

  private updateSession(patch: Partial<SessionInfo>, bumpRevision = true): number {
    const current = this.state.session;
    const session = { ...current, ...patch, revision: current.revision + (bumpRevision ? 1 : 0) };
    this.state = { ...this.state, session };
    this.emit({ type: "session_updated", session });
    return session.revision;
  }

  private updateMaster(patch: Partial<MasterStrip>, bumpRevision = true): number {
    const current = this.state.master;
    const master = { ...current, ...patch, revision: current.revision + (bumpRevision ? 1 : 0) };
    this.state = { ...this.state, master };
    this.emit({ type: "master_updated", master });
    return master.revision;
  }

  private updateChannel(
    entityId: string,
    patch: Partial<ChannelStrip>,
    bumpRevision = true,
  ): number {
    let revision = 0;
    const channels = this.state.channels.map((c) => {
      if (c.entity_id !== entityId) return c;
      const next = { ...c, ...patch, revision: c.revision + (bumpRevision ? 1 : 0) };
      revision = next.revision;
      this.emit({ type: "channel_updated", channel: next });
      return next;
    });
    this.state = { ...this.state, channels };
    return revision;
  }
}
