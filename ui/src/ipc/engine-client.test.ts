// SPDX-License-Identifier: AGPL-3.0-only
import { beforeEach, describe, expect, it } from "vitest";
import { FakeTimers } from "../utils/fake-timers";
import {
  EngineClient,
  kContinuousAckTimeoutMs,
  kContinuousSendIntervalMs,
  kDiscreteAckTimeoutMs,
  type ContinuousOutcome,
} from "./engine-client";
import type { EngineToUiMessage, SetGainCommand, UiToEngineMessage } from "./messages";
import type { MessageListener, Transport } from "./transport";

/** 送ったメッセージを記録し、テストから任意のメッセージを届けられる Transport。 */
class RecordingTransport implements Transport {
  readonly sent: UiToEngineMessage[] = [];
  private listener: MessageListener | undefined;
  send(message: UiToEngineMessage): void {
    this.sent.push(message);
  }
  subscribe(listener: MessageListener): () => void {
    this.listener = listener;
    return () => undefined;
  }
  deliver(message: EngineToUiMessage): void {
    this.listener?.(message);
  }
  gains(): SetGainCommand[] {
    return this.sent.filter((m): m is SetGainCommand => m.type === "set_gain");
  }
}

let timers: FakeTimers;
let transport: RecordingTransport;
let client: EngineClient;
let outcomes: [string, ContinuousOutcome][];

beforeEach(() => {
  timers = new FakeTimers();
  timers.advance(1000);
  transport = new RecordingTransport();
  client = new EngineClient(transport, timers, "r", "a1");
  outcomes = [];
  client.onContinuousOutcome((id, outcome) => outcomes.push([id, outcome]));
});

describe("EngineClient の離散コマンド", () => {
  it("接続を共有する窓ごとの接頭辞で request_id を作り、ほかの窓の応答では解決しない", async () => {
    const routing = new EngineClient(transport, timers, "rt", "b2");
    const result = routing.sendCommand("routing", 1, { type: "discard_routing" });
    const sent = transport.sent[0];
    if (sent === undefined || !("request_id" in sent)) throw new Error("unreachable");
    expect(sent.request_id).toBe("rt-b2.1");
    // ミキサーの窓のコマンド（r-a1.1）への応答は、ルーティングの窓には関係がない。
    transport.deliver({ type: "reject", request_id: "r-a1.1", error_code: "NOT_FOUND" });
    transport.deliver({ type: "ack", request_id: "rt-b2.1", applied_revision: 2 });
    await expect(result).resolves.toEqual({ ok: true, applied_revision: 2 });
  });

  it("開き直した画面のクライアントは、前の画面と重ならない request_id を振る", () => {
    // エンジンは画面の再表示にまたがって request_id の重複を覚えている。新しい画面の最初の
    // コマンドが前の画面の最初のコマンドと同じ request_id だと、重複として捨てられる。
    const before = new EngineClient(transport, timers, "m");
    void before.sendCommand("master", 1, { type: "set_master_mute", muted: false });
    const reopened = new EngineClient(transport, timers, "m");
    void reopened.sendCommand("master", 2, { type: "set_master_mute", muted: true });
    const ids = transport.sent.map((m) => ("request_id" in m ? m.request_id : undefined));
    expect(ids).toHaveLength(2);
    expect(ids[0]).not.toBe(ids[1]);
    expect(before.origin).not.toBe(reopened.origin);
    expect(ids[0]?.startsWith(`${before.origin}.`)).toBe(true);
    expect(ids[1]?.startsWith(`${reopened.origin}.`)).toBe(true);
  });

  it("request_id と expected_revision を付けて送り、ACK で解決する", async () => {
    const result = client.sendCommand("ch1", 4, { type: "set_channel_mute", muted: false });
    const sent = transport.sent[0];
    expect(sent).toMatchObject({
      type: "set_channel_mute",
      entity_id: "ch1",
      expected_revision: 4,
      muted: false,
    });
    if (sent === undefined || !("request_id" in sent)) throw new Error("unreachable");
    transport.deliver({ type: "ack", request_id: sent.request_id, applied_revision: 5 });
    await expect(result).resolves.toEqual({ ok: true, applied_revision: 5 });
  });

  it("拒否されたらエラーコードを返す", async () => {
    const result = client.sendCommand("ch1", 4, { type: "remove_strip" });
    const sent = transport.sent[0] as { request_id: string };
    transport.deliver({ type: "reject", request_id: sent.request_id, error_code: "INVALID_STATE" });
    await expect(result).resolves.toEqual({ ok: false, error_code: "INVALID_STATE" });
  });

  it("期限内に応答がなければ TIMEOUT を返す", async () => {
    const result = client.sendCommand("ch1", 4, { type: "set_channel_mute", muted: true });
    timers.advance(kDiscreteAckTimeoutMs);
    await expect(result).resolves.toEqual({ ok: false, error_code: "TIMEOUT" });
  });
});

describe("EngineClient の連続値", () => {
  it("60 Hz より速い入力を間引き、最新値だけを後で送る", () => {
    client.setContinuous("ch1", -1);
    client.setContinuous("ch1", -2);
    client.setContinuous("ch1", -3);
    expect(transport.gains().map((g) => g.gain_db)).toEqual([-1]);
    timers.advance(kContinuousSendIntervalMs);
    expect(transport.gains().map((g) => g.gain_db)).toEqual([-1, -3]);
    expect(transport.gains().map((g) => g.seq)).toEqual([1, 2]);
  });

  it("flush で保留中の値をすぐに送る", () => {
    client.setContinuous("ch1", -1);
    client.setContinuous("ch1", -5);
    client.flush("ch1");
    expect(transport.gains().map((g) => g.gain_db)).toEqual([-1, -5]);
  });

  it("set_gain に送り元を付け、開き直した画面では別の送り元で seq を 1 から数える", () => {
    client.setContinuous("master", -1);
    const reopened = new EngineClient(transport, timers, "r", "c3");
    reopened.setContinuous("master", -30);
    expect(transport.gains().map((g) => [g.origin, g.seq])).toEqual([
      ["r-a1", 1],
      ["r-c3", 1],
    ]);
  });

  it("接続を共有するほかの窓の送った値への gain_ack では settled にしない", () => {
    client.setContinuous("bus-cue", -1);
    transport.deliver({
      type: "gain_ack",
      entity_id: "bus-cue",
      seq: 5,
      applied_revision: 1,
      origin: "rt-b2",
    });
    expect(outcomes).toEqual([]);
    transport.deliver({
      type: "gain_ack",
      entity_id: "bus-cue",
      seq: 1,
      applied_revision: 1,
      origin: "r-a1",
    });
    expect(outcomes).toEqual([["bus-cue", "settled"]]);
  });

  it("最後の送信までの ACK がそろったら settled を知らせる", () => {
    client.setContinuous("ch1", -1);
    timers.advance(kContinuousSendIntervalMs);
    client.setContinuous("ch1", -2);
    transport.deliver({
      type: "gain_ack",
      entity_id: "ch1",
      seq: 1,
      applied_revision: 1,
      origin: "r-a1",
    });
    expect(outcomes).toEqual([]);
    transport.deliver({
      type: "gain_ack",
      entity_id: "ch1",
      seq: 2,
      applied_revision: 1,
      origin: "r-a1",
    });
    expect(outcomes).toEqual([["ch1", "settled"]]);
  });

  it("送ってから 500 ms 以内に ACK が来なければ timeout を知らせる", () => {
    client.setContinuous("ch1", -1);
    timers.advance(kContinuousAckTimeoutMs - 1);
    expect(outcomes).toEqual([]);
    timers.advance(1);
    expect(outcomes).toEqual([["ch1", "timeout"]]);
  });

  it("送り続けていても、最も古い未 ACK の送信から 500 ms で timeout にする", () => {
    for (let i = 0; i < 40; i++) {
      client.setContinuous("ch1", -i);
      timers.advance(kContinuousSendIntervalMs);
    }
    expect(outcomes).toEqual([["ch1", "timeout"]]);
  });

  it("期限切れのあとに届いた古い ACK では settled を知らせない", () => {
    client.setContinuous("ch1", -1);
    timers.advance(kContinuousAckTimeoutMs);
    transport.deliver({
      type: "gain_ack",
      entity_id: "ch1",
      seq: 1,
      applied_revision: 1,
      origin: "r-a1",
    });
    expect(outcomes).toEqual([["ch1", "timeout"]]);
  });

  it("entity ごとにシーケンス番号を持つ", () => {
    client.setContinuous("ch1", -1);
    client.setContinuous("master", -2);
    expect(transport.gains().map((g) => [g.entity_id, g.seq])).toEqual([
      ["ch1", 1],
      ["master", 1],
    ]);
  });
});
