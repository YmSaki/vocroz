// SPDX-License-Identifier: AGPL-3.0-only
// 外部出力のパネルの受け入れ。フェイクエンジンにつないだミキサー画面で、
// 設定の保存、鍵の設定、開始と停止、接続テストがエンジンの状態どおりに表示されることを確かめる。
// 鍵がなければ開始できないこと、設定した鍵が画面に残らないことも確かめる。
import { act, fireEvent, render, screen } from "@testing-library/preact";
import { beforeEach, describe, expect, it } from "vitest";
import { EngineClient } from "../../ipc/engine-client";
import { FakeMixerEngine } from "../../ipc/fake-engine/fake-mixer-engine";
import { kFakeExternalOutputTestMs } from "../../ipc/fake-engine/fake-external-output";
import { LinkStatsStore } from "../../state/link-stats-store";
import { MeterStore } from "../../state/meter-store";
import { MixerStore } from "../../state/mixer-store";
import { OptimisticStore } from "../../state/optimistic-store";
import { ToastStore } from "../../state/toast-store";
import { FakeTimers } from "../../utils/fake-timers";
import { connectNotices } from "../shared/feedback";
import { MixerScreen } from "./MixerScreen";

let timers: FakeTimers;
let engine: FakeMixerEngine;
let toasts: ToastStore;
let copied: string[];

async function advance(ms: number) {
  await act(async () => timers.advance(ms));
}

function panel(): HTMLElement {
  return screen.getByRole("complementary", { name: "外部出力（SRTP）" });
}

function button(name: string): HTMLButtonElement {
  return screen.getByRole("button", { name }) as HTMLButtonElement;
}

async function configure(host: string) {
  fireEvent.input(screen.getByLabelText("宛先（ホスト名か IP アドレス）"), {
    target: { value: host },
  });
  fireEvent.click(button("設定を保存"));
  await advance(20);
  fireEvent.click(button("鍵を作る"));
  fireEvent.click(button("鍵を設定"));
  await advance(20);
}

beforeEach(async () => {
  timers = new FakeTimers();
  engine = new FakeMixerEngine({ timers, meterIntervalMs: 0, latencyMs: 10 });
  const client = new EngineClient(engine, timers);
  const store = new MixerStore();
  toasts = new ToastStore(timers, () => 0);
  copied = [];
  client.subscribe((m) => store.apply(m));
  connectNotices(client, toasts);
  render(
    <MixerScreen
      client={client}
      store={store}
      optimistic={new OptimisticStore()}
      meters={new MeterStore(timers)}
      linkStats={new LinkStatsStore()}
      toasts={toasts}
      onOpenRouting={() => {}}
      writeClipboard={(text) => {
        copied.push(text);
        return Promise.resolve();
      }}
    />,
  );
  client.hello();
  await advance(20);
  fireEvent.click(button("外部出力"));
});

describe("外部出力のパネル", () => {
  it("起動時は停止中で、宛先と鍵がなければ開始も接続テストもできない", () => {
    expect(panel().textContent).toContain("停止中");
    expect(panel().textContent).toContain("鍵が未設定です");
    expect(button("送信を開始").disabled).toBe(true);
    expect(button("接続テスト").disabled).toBe(true);
  });

  it("設定と鍵を送ると開始でき、エンジンの状態どおりに送信中と表示する", async () => {
    await configure("192.0.2.10");
    expect(engine.snapshot().external_output).toMatchObject({
      key_configured: true,
      config: { host: "192.0.2.10" },
    });
    expect(panel().textContent).toContain("鍵は設定済みです");

    fireEvent.click(button("送信を開始"));
    await advance(20);
    expect(engine.snapshot().external_output.state).toBe("RUNNING");
    expect(panel().textContent).toContain("送信中");
    expect(panel().textContent).toContain("送信できました");
    // 送信中は設定を変えられない。
    expect(
      (screen.getByLabelText("宛先（ホスト名か IP アドレス）") as HTMLInputElement).disabled,
    ).toBe(true);

    fireEvent.click(button("送信を停止"));
    await advance(20);
    expect(engine.snapshot().external_output.state).toBe("STOPPED");
  });

  it("設定した鍵は入力欄に残さない", async () => {
    fireEvent.click(button("鍵を作る"));
    const input = screen.getByLabelText("SRTP の鍵（inline: 形式）") as HTMLInputElement;
    // 作った鍵は受信機へ貼れるよう見える状態にし、コピーできる。
    expect(input.type).toBe("text");
    expect(input.value).toMatch(/^inline:[A-Za-z0-9+/]{40}$/);
    fireEvent.click(button("コピー"));
    expect(copied).toEqual([input.value]);

    fireEvent.click(button("鍵を設定"));
    await advance(20);
    expect(input.value).toBe("");
    expect(input.type).toBe("password");
  });

  it("保存していない変更があるときは開始できない", async () => {
    await configure("192.0.2.10");
    fireEvent.input(screen.getByLabelText("ポート"), { target: { value: "6000" } });
    expect(button("送信を開始").disabled).toBe(true);
  });

  it("接続テストは数秒で終わり、結果を表示する", async () => {
    await configure("192.0.2.10");
    fireEvent.click(button("接続テスト"));
    await advance(20);
    expect(panel().textContent).toContain("接続テスト中");
    expect(button("送信を開始").disabled).toBe(true);
    await advance(kFakeExternalOutputTestMs);
    expect(panel().textContent).toContain("停止中");
    expect(panel().textContent).toContain("送信できました");
  });

  it("開始に失敗したら停止のままで、結果と通知を出す", async () => {
    await configure("receiver.invalid");
    fireEvent.click(button("送信を開始"));
    await advance(20);
    expect(panel().textContent).toContain("宛先の名前を解決できませんでした");
    expect(toasts.get().history.map((item) => item.text)).toContain(
      "外部出力を開始できませんでした。宛先と鍵の設定を確かめてください。",
    );
  });
});
