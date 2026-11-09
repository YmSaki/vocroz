// SPDX-License-Identifier: AGPL-3.0-only
// 開発用のフェイクエンジンの共通部分。
// ブラウザだけで画面を操作できるよう、エンジンの代わりにコマンドを受けて ACK と状態を返す。
// 本物のエンジンと同じく、受け取ったコマンドは少し遅れて適用し、その後に ACK と状態の更新を送る。
// 正となる状態はフェイクエンジンが持ち、画面側は届いた状態を表示するだけにする（エンジン主権の再現）。

import type { DiscreteCommand, EngineToUiMessage, ErrorCode, UiToEngineMessage } from "../messages";
import type { MessageListener, Transport } from "../transport";
import { kBrowserTimers, type Timers } from "../../utils/timers";

/** フェイクエンジンの振る舞いの設定。 */
export interface FakeEngineOptions {
  /** コマンドを適用して応答するまでの遅れ（ms）。 */
  latencyMs?: number;
  /** true なら ACK を返さない（ACK が来ないときの画面の振る舞いを確かめるため）。 */
  dropAcks?: boolean;
  /** メーターを送る周期（ms）。0 なら送らない。 */
  meterIntervalMs?: number;
  timers?: Timers;
  /** メーターの値を作る乱数。テストでは決定的な関数を渡す。 */
  random?: () => number;
}

/** 既定の応答の遅れ（ms）。ローカルの IPC の往復に近い値にする。 */
const kDefaultLatencyMs = 15;
/** 既定のメーターの周期（ms）。エンジンが送る 10〜30 Hz の中ほど（20 Hz）。 */
const kDefaultMeterIntervalMs = 50;

/** フェイクエンジンの共通部分。モードごとのフェイクエンジンが継承する。 */
export abstract class FakeEngineBase implements Transport {
  protected readonly timers: Timers;
  protected readonly random: () => number;
  protected dropAcks: boolean;
  private readonly latencyMs: number;
  private readonly listeners = new Set<MessageListener>();
  private meterTimer: unknown;

  constructor(options: FakeEngineOptions = {}) {
    this.timers = options.timers ?? kBrowserTimers;
    this.random = options.random ?? Math.random;
    this.dropAcks = options.dropAcks ?? false;
    this.latencyMs = options.latencyMs ?? kDefaultLatencyMs;
    const meterIntervalMs = options.meterIntervalMs ?? kDefaultMeterIntervalMs;
    if (meterIntervalMs > 0) this.startMeters(meterIntervalMs);
  }

  send(message: UiToEngineMessage): void {
    this.later(() => this.receive(message));
  }

  subscribe(listener: MessageListener): () => void {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }

  /** ACK を返すかを切り替える（開発時にブラウザのコンソールから使う）。 */
  setDropAcks(drop: boolean): void {
    this.dropAcks = drop;
  }

  /** メーターの送信を止める。 */
  dispose(): void {
    if (this.meterTimer !== undefined) this.timers.clearTimeout(this.meterTimer);
    this.meterTimer = undefined;
  }

  /** UI からのメッセージを処理する。 */
  protected abstract receive(message: UiToEngineMessage): void;

  /** メーターの値を作る。 */
  protected abstract meterLevels(): Record<string, number | null>;

  /**
   * メーターを送るたびに呼ぶ。メーターより遅い周期の観測値（受信の統計値など）を送るのに使う。
   * @param intervalMs メーターの周期（ms）
   */
  protected onMeterTick(intervalMs: number): void {
    void intervalMs;
  }

  /** UI へメッセージを送る。 */
  protected emit(message: EngineToUiMessage): void {
    for (const listener of this.listeners) listener(message);
  }

  /** 少し遅れて処理する。 */
  protected later(action: () => void, delayMs = this.latencyMs): void {
    this.timers.setTimeout(action, delayMs);
  }

  /** 離散コマンドの適用完了を返す。 */
  protected ack(command: DiscreteCommand, appliedRevision: number): void {
    if (this.dropAcks) return;
    this.emit({ type: "ack", request_id: command.request_id, applied_revision: appliedRevision });
  }

  /** 離散コマンドの拒否を返す。 */
  protected reject(command: DiscreteCommand, errorCode: ErrorCode): void {
    if (this.dropAcks) return;
    this.emit({ type: "reject", request_id: command.request_id, error_code: errorCode });
  }

  /**
   * 信号がある入力のそれらしいレベル（dBFS）を作る。
   * @param baseDb 平均的なレベル
   */
  protected signalLevel(baseDb: number): number {
    return Math.min(0, baseDb + (this.random() - 0.5) * 18);
  }

  private startMeters(intervalMs: number): void {
    const tick = () => {
      this.emit({ type: "meters", levels: this.meterLevels() });
      this.onMeterTick(intervalMs);
      this.meterTimer = this.timers.setTimeout(tick, intervalMs);
    };
    this.meterTimer = this.timers.setTimeout(tick, intervalMs);
  }
}
