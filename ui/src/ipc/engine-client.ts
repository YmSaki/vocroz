// SPDX-License-Identifier: AGPL-3.0-only
// エンジンへのコマンドの送信を受け持つクライアント。
//
// 離散コマンド（ミュートなど）は request_id を付けて送り、ACK か拒否が届くまで待つ。
// 表示は ACK のあとにエンジンから届く状態で更新するので、ここでは結果を返すだけにする。
//
// 連続値（フェーダーなど）はドラッグ中に大量に発生するため、entity ごとに 60 Hz に間引いて送る。
// 送ってから 500 ms 以内に ACK が来なければ、エンジンに届いていないとみなして呼び出し側に知らせる。
// 呼び出し側は楽観的に更新した表示を元に戻し、警告を出す。

import type {
  DiscreteCommandBody,
  EngineToUiMessage,
  ErrorCode,
  GainDb,
  UiToEngineMessage,
} from "./messages";
import type { MessageListener, Transport } from "./transport";
import { kBrowserTimers, type Timers } from "../utils/timers";

/** 離散コマンドの結果。 */
export type CommandResult =
  { ok: true; applied_revision: number } | { ok: false; error_code: ErrorCode | "TIMEOUT" };

/** 連続値の送信の結果。settled はすべて適用済み、timeout は ACK が期限内に届かなかったことを表す。 */
export type ContinuousOutcome = "settled" | "timeout";

/** 連続値の送信の結果を受け取るリスナー。 */
export type ContinuousListener = (entityId: string, outcome: ContinuousOutcome) => void;

/**
 * 連続値を送る最小間隔（ms）。約 60 Hz。画面の更新周期より細かく送っても操作者には区別できないため。
 * 1000 / 60 だと時刻の加算の丸め誤差で間隔の判定がぶれるので、整数の 16 ms にする。
 */
export const kContinuousSendIntervalMs = 16;
/** 連続値の ACK を待つ時間（ms）。これを超えたら表示を戻して警告する。 */
export const kContinuousAckTimeoutMs = 500;
/**
 * 離散コマンドの ACK を待つ時間（ms）。
 * 操作の応答の目標（p95 で 100 ms）より十分長くし、通常の遅れでは警告しないようにする。
 */
export const kDiscreteAckTimeoutMs = 2000;

/** entity ごとの連続値の送信状態。 */
interface ContinuousChannel {
  /** 次に送るシーケンス番号。 */
  nextSeq: number;
  /** 間引きのために送らずに保留している最新値。 */
  pendingValue: GainDb | undefined;
  /** 最後に送った時刻。 */
  lastSentAt: number;
  /** 保留中の値を送るタイマー。 */
  flushTimer: unknown;
  /** ACK を待っている送信（シーケンス番号と送信時刻）。古い順。 */
  unacked: { seq: number; sentAt: number }[];
  /** 最も古い未 ACK の送信の期限のタイマー。 */
  timeoutTimer: unknown;
}

/** 離散コマンドの ACK 待ち。 */
interface PendingRequest {
  resolve: (result: CommandResult) => void;
  timer: unknown;
}

/** エンジンへのコマンドの送信と、ACK の待ち合わせを行うクライアント。 */
export class EngineClient {
  private nextRequestId = 1;
  private readonly pending = new Map<string, PendingRequest>();
  private readonly continuous = new Map<string, ContinuousChannel>();
  private readonly continuousListeners = new Set<ContinuousListener>();

  /**
   * @param transport エンジンとの通信経路
   * @param timers タイマー（テストではフェイクを渡す）
   */
  constructor(
    private readonly transport: Transport,
    private readonly timers: Timers = kBrowserTimers,
  ) {
    transport.subscribe((message) => this.handle(message));
  }

  /** エンジンに接続したことを知らせ、全状態のスナップショットを要求する。 */
  hello(): void {
    this.transport.send({ type: "hello" });
  }

  /**
   * エンジンからのメッセージを購読する。ACK も含めてすべてのメッセージが届く。
   * @returns 購読を解除する関数
   */
  subscribe(listener: MessageListener): () => void {
    return this.transport.subscribe(listener);
  }

  /**
   * 離散コマンドを送り、ACK か拒否を待つ。
   * @param entityId 操作する entity
   * @param expectedRevision UI が表示している entity の revision
   * @param body コマンドの本体
   * @returns 結果。期限内に応答がなければ TIMEOUT
   */
  sendCommand(
    entityId: string,
    expectedRevision: number,
    body: DiscreteCommandBody,
  ): Promise<CommandResult> {
    const requestId = `r${this.nextRequestId++}`;
    return new Promise((resolve) => {
      const timer = this.timers.setTimeout(() => {
        this.pending.delete(requestId);
        resolve({ ok: false, error_code: "TIMEOUT" });
      }, kDiscreteAckTimeoutMs);
      this.pending.set(requestId, { resolve, timer });
      const message: UiToEngineMessage = {
        ...body,
        request_id: requestId,
        entity_id: entityId,
        expected_revision: expectedRevision,
      };
      this.transport.send(message);
    });
  }

  /**
   * 連続値を送る。60 Hz より速く呼ばれた分は間引き、最新値だけを後で送る。
   * @param entityId 操作する entity
   * @param value 新しい値
   */
  setContinuous(entityId: string, value: GainDb): void {
    const channel = this.channelFor(entityId);
    const elapsed = this.timers.now() - channel.lastSentAt;
    if (elapsed >= kContinuousSendIntervalMs && channel.flushTimer === undefined) {
      this.sendContinuousNow(entityId, channel, value);
      return;
    }
    channel.pendingValue = value;
    if (channel.flushTimer === undefined) {
      channel.flushTimer = this.timers.setTimeout(
        () => this.flush(entityId),
        Math.max(0, kContinuousSendIntervalMs - elapsed),
      );
    }
  }

  /**
   * 間引きで保留している値をすぐに送る。ドラッグの終了時に呼び、最後の値を確実に届ける。
   * @param entityId 操作した entity
   */
  flush(entityId: string): void {
    const channel = this.continuous.get(entityId);
    if (channel === undefined) return;
    if (channel.flushTimer !== undefined) {
      this.timers.clearTimeout(channel.flushTimer);
      channel.flushTimer = undefined;
    }
    if (channel.pendingValue !== undefined) {
      const value = channel.pendingValue;
      channel.pendingValue = undefined;
      this.sendContinuousNow(entityId, channel, value);
    }
  }

  /**
   * 連続値の送信の結果を購読する。
   * @returns 購読を解除する関数
   */
  onContinuousOutcome(listener: ContinuousListener): () => void {
    this.continuousListeners.add(listener);
    return () => this.continuousListeners.delete(listener);
  }

  private channelFor(entityId: string): ContinuousChannel {
    let channel = this.continuous.get(entityId);
    if (channel === undefined) {
      channel = {
        nextSeq: 1,
        pendingValue: undefined,
        lastSentAt: Number.NEGATIVE_INFINITY,
        flushTimer: undefined,
        unacked: [],
        timeoutTimer: undefined,
      };
      this.continuous.set(entityId, channel);
    }
    return channel;
  }

  private sendContinuousNow(entityId: string, channel: ContinuousChannel, value: GainDb): void {
    const seq = channel.nextSeq++;
    const now = this.timers.now();
    channel.lastSentAt = now;
    channel.unacked.push({ seq, sentAt: now });
    if (channel.timeoutTimer === undefined) this.scheduleTimeout(entityId, channel);
    this.transport.send({ type: "set_gain", entity_id: entityId, seq, gain_db: value });
  }

  /** 最も古い未 ACK の送信の期限にタイマーを合わせる。 */
  private scheduleTimeout(entityId: string, channel: ContinuousChannel): void {
    if (channel.timeoutTimer !== undefined) {
      this.timers.clearTimeout(channel.timeoutTimer);
      channel.timeoutTimer = undefined;
    }
    const oldest = channel.unacked[0];
    if (oldest === undefined) return;
    const remaining = oldest.sentAt + kContinuousAckTimeoutMs - this.timers.now();
    channel.timeoutTimer = this.timers.setTimeout(
      () => this.expire(entityId),
      Math.max(0, remaining),
    );
  }

  private expire(entityId: string): void {
    const channel = this.continuous.get(entityId);
    if (channel === undefined) return;
    channel.timeoutTimer = undefined;
    // 期限切れの後に古い ACK が届いても、戻した表示を再び動かさないよう、待ちを捨てる。
    channel.unacked = [];
    if (channel.flushTimer !== undefined) {
      this.timers.clearTimeout(channel.flushTimer);
      channel.flushTimer = undefined;
    }
    channel.pendingValue = undefined;
    this.emitContinuous(entityId, "timeout");
  }

  private handle(message: EngineToUiMessage): void {
    switch (message.type) {
      case "ack":
        this.settle(message.request_id, { ok: true, applied_revision: message.applied_revision });
        return;
      case "reject":
        this.settle(message.request_id, { ok: false, error_code: message.error_code });
        return;
      case "gain_ack":
        this.acknowledgeContinuous(message.entity_id, message.seq);
        return;
      case "mixer_snapshot":
      case "client_snapshot":
      case "session_updated":
      case "channel_updated":
      case "channel_removed":
      case "master_updated":
      case "pending_updated":
      case "client_updated":
      case "meters":
      case "notice":
        return;
    }
  }

  private settle(requestId: string, result: CommandResult): void {
    const request = this.pending.get(requestId);
    if (request === undefined) return;
    this.timers.clearTimeout(request.timer);
    this.pending.delete(requestId);
    request.resolve(result);
  }

  private acknowledgeContinuous(entityId: string, seq: number): void {
    const channel = this.continuous.get(entityId);
    if (channel === undefined || channel.unacked.length === 0) return;
    // ACK の seq 以下はすべて適用済み（後勝ちなので、それより古い値は上書きされている）。
    channel.unacked = channel.unacked.filter((entry) => entry.seq > seq);
    this.scheduleTimeout(entityId, channel);
    if (channel.unacked.length === 0 && channel.pendingValue === undefined) {
      this.emitContinuous(entityId, "settled");
    }
  }

  private emitContinuous(entityId: string, outcome: ContinuousOutcome): void {
    for (const listener of this.continuousListeners) listener(entityId, outcome);
  }
}
