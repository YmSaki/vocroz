// SPDX-License-Identifier: AGPL-3.0-only
// 子機モードのフェイクエンジン。子機ロジック（session、transport、capture）の代わりに、
// 子機の画面の操作を受けて状態を返す。
//
// 招待に次の語を含めると、失敗の流れを確かめられる。
// - "expired": 招待の期限切れ（INVITE_EXPIRED）
// - "reject": 親機が参加を拒否（REJECTED）
// - "unreachable": NAT 越えの不成立（UNREACHABLE）

import type {
  ClientSnapshot,
  ClientState,
  DiscreteCommand,
  ErrorCode,
  UiToEngineMessage,
} from "../messages";
import { FakeEngineBase, type FakeEngineOptions } from "./fake-engine-base";

const kClientId = "client";
/** 招待の解決から接続までの各段階の待ち時間（ms）。画面で遷移を目で追える長さにする。 */
const kStepDelayMs = 500;
/** 親機の承認を待つ時間（ms）。 */
const kApprovalDelayMs = 2500;

function initialSnapshot(): ClientSnapshot {
  return {
    entity_id: kClientId,
    revision: 1,
    client_state: "DISCONNECTED",
    failure_code: null,
    session_name: null,
    input_devices: [
      { id: "{0.0.1.00000000}.{mic-usb}", name: "マイク (USB Audio Interface)" },
      { id: "{0.0.1.00000000}.{line-realtek}", name: "ライン入力 (Realtek Audio)" },
    ],
    selected_input_id: null,
    input_missing: false,
    // 起動時は必ず送信ミュートから始める。
    send_muted: true,
    send_level_db: 0,
  };
}

/** 子機モードのフェイクエンジン。 */
export class FakeClientEngine extends FakeEngineBase {
  private state: ClientSnapshot = initialSnapshot();
  /** 接続の試行の世代。取り消しや再試行のあとに、古い試行の遷移を適用しないために使う。 */
  private attempt = 0;

  constructor(options: FakeEngineOptions = {}) {
    super(options);
  }

  /** 現在の状態（テストでの確認用）。 */
  snapshot(): ClientSnapshot {
    return this.state;
  }

  /** 選択中のマイクの抜去を再現する。送信ミュートの状態は変えない。 */
  simulateInputMissing(): void {
    // 入力の欠落は観測値なので revision を進めない（その最中の送信ミュートを衝突させないため）。
    this.update({ input_missing: true }, false);
    this.emit({ type: "notice", severity: "warning", code: "input_missing", params: {} });
  }

  /** 回線の途絶と再接続を再現する。 */
  simulateReconnect(): void {
    if (this.state.client_state !== "CONNECTED") return;
    this.update({ client_state: "RECONNECTING" }, false);
    const attempt = this.attempt;
    this.later(() => {
      if (attempt === this.attempt) this.update({ client_state: "CONNECTED" }, false);
    }, kApprovalDelayMs);
  }

  protected receive(message: UiToEngineMessage): void {
    switch (message.type) {
      case "hello":
        this.emit({ type: "client_snapshot", snapshot: this.state });
        return;
      case "set_gain":
        if (message.entity_id !== kClientId) return;
        // 連続値は seq の後勝ちで、revision を進めない（離散操作の expected_revision と衝突させないため）。
        this.update({ send_level_db: message.gain_db ?? -60 }, false);
        if (this.dropAcks) return;
        this.emit({
          type: "gain_ack",
          entity_id: kClientId,
          seq: message.seq,
          applied_revision: this.state.revision,
        });
        return;
      case "join":
      case "leave":
      case "select_input":
      case "set_send_mute":
      case "create_session":
      case "end_session":
      case "set_channel_mute":
      case "set_master_mute":
      case "approve_participant":
      case "reject_participant":
      case "remove_strip":
        this.applyCommand(message);
        return;
    }
  }

  protected meterLevels(): Record<string, number | null> {
    // 入力レベルは送信ミュート中も表示し、参加する前にマイクを確かめられるようにする。
    const hasInput = this.state.selected_input_id !== null && !this.state.input_missing;
    return { [kClientId]: hasInput ? this.signalLevel(-24) : null };
  }

  private applyCommand(command: DiscreteCommand): void {
    if (command.entity_id !== kClientId) return this.reject(command, "NOT_FOUND");
    if (command.expected_revision !== this.state.revision) {
      return this.reject(command, "REVISION_CONFLICT");
    }
    switch (command.type) {
      case "join":
        if (this.state.client_state !== "DISCONNECTED" && this.state.client_state !== "FAILED") {
          return this.reject(command, "INVALID_STATE");
        }
        this.ack(command, this.update({ client_state: "RESOLVING", failure_code: null }));
        this.runJoin(command.invite);
        return;
      case "leave":
        this.attempt++;
        this.ack(
          command,
          this.update({ client_state: "DISCONNECTED", failure_code: null, session_name: null }),
        );
        return;
      case "select_input":
        // マイクを選んでも送信は始めない（send_muted はそのまま）。
        this.ack(
          command,
          this.update({ selected_input_id: command.device_id, input_missing: false }),
        );
        return;
      case "set_send_mute":
        this.ack(command, this.update({ send_muted: command.muted }));
        return;
      case "create_session":
      case "end_session":
      case "set_channel_mute":
      case "set_master_mute":
      case "approve_participant":
      case "reject_participant":
      case "remove_strip":
        this.reject(command, "INVALID_STATE");
        return;
    }
  }

  /** 招待の解決から接続までを順に進める。 */
  private runJoin(invite: string): void {
    const attempt = ++this.attempt;
    const lower = invite.toLowerCase();
    const failWith = (code: ErrorCode) =>
      this.update({ client_state: "FAILED", failure_code: code }, false);
    const steps: { state: ClientState; delayMs: number; failure?: ErrorCode }[] = [
      { state: "AUTHENTICATING", delayMs: kStepDelayMs },
      { state: "WAITING_APPROVAL", delayMs: kStepDelayMs },
      { state: "CONNECTING", delayMs: kApprovalDelayMs },
      { state: "CONNECTED", delayMs: kStepDelayMs },
    ];
    if (lower.includes("expired"))
      steps.splice(0, steps.length, {
        state: "FAILED",
        delayMs: kStepDelayMs,
        failure: "INVITE_EXPIRED",
      });
    else if (lower.includes("reject"))
      steps.splice(2, 2, { state: "FAILED", delayMs: kApprovalDelayMs, failure: "REJECTED" });
    else if (lower.includes("unreachable"))
      steps.splice(3, 1, { state: "FAILED", delayMs: kStepDelayMs, failure: "UNREACHABLE" });

    const run = (index: number) => {
      const step = steps[index];
      if (step === undefined) return;
      this.later(() => {
        if (attempt !== this.attempt) return;
        if (step.failure !== undefined) {
          failWith(step.failure);
          return;
        }
        // 接続の進行は観測値なので revision を進めない。進めると、接続処理の途中の送信ミュートが
        // REVISION_CONFLICT になる（docs/adr/0015-control-protocol-messages.md）。
        this.update(
          {
            client_state: step.state,
            session_name: step.state === "RESOLVING" ? null : "日曜リハーサル",
          },
          false,
        );
        run(index + 1);
      }, step.delayMs);
    };
    run(0);
  }

  private update(patch: Partial<ClientSnapshot>, bumpRevision = true): number {
    this.state = {
      ...this.state,
      ...patch,
      revision: this.state.revision + (bumpRevision ? 1 : 0),
    };
    this.emit({ type: "client_updated", client: this.state });
    return this.state.revision;
  }
}
