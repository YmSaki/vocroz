// SPDX-License-Identifier: AGPL-3.0-only
// 子機の画面の受け入れ。
// - 送信はミュートから始まり、マイクを選んだだけでは送信を始めない。
// - 招待の入力から承認待ちを経て接続でき、失敗は原因別の文言で表示する。
// - AT-A09 の UI 部分: 開発中のボタンはトーストを出すだけで、ウィンドウも状態も変えない。
import { act, fireEvent, render, screen } from "@testing-library/preact";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { EngineClient } from "../../ipc/engine-client";
import { FakeClientEngine } from "../../ipc/fake-engine/fake-client-engine";
import { ClientStore } from "../../state/client-store";
import { MeterStore } from "../../state/meter-store";
import { OptimisticStore } from "../../state/optimistic-store";
import { ToastStore } from "../../state/toast-store";
import { FakeTimers } from "../../utils/fake-timers";
import { ClientScreen } from "./ClientScreen";

let timers: FakeTimers;
let engine: FakeClientEngine;
let toasts: ToastStore;

/** 時刻を進め、ACK を待つ Promise の解決と再描画まで済ませる。 */
async function advance(ms: number) {
  await act(async () => timers.advance(ms));
}

beforeEach(async () => {
  timers = new FakeTimers();
  engine = new FakeClientEngine({ timers, meterIntervalMs: 0, latencyMs: 10 });
  const client = new EngineClient(engine, timers);
  const store = new ClientStore();
  toasts = new ToastStore(timers, () => 0);
  client.subscribe((m) => store.apply(m));
  render(
    <ClientScreen
      client={client}
      store={store}
      optimistic={new OptimisticStore()}
      meters={new MeterStore(timers)}
      toasts={toasts}
      initialInvite="7KQ2-M9XD"
    />,
  );
  client.hello();
  await advance(20);
});

afterEach(() => vi.restoreAllMocks());

function sendMute(): HTMLElement {
  return screen.getByRole("button", { name: "送信ミュート" });
}

describe("送信の安全", () => {
  it("送信ミュートから始まり、マイクを選んでも送信を始めない", async () => {
    expect(sendMute().getAttribute("aria-pressed")).toBe("true");
    fireEvent.change(screen.getByRole("combobox", { name: "入力デバイス" }), {
      target: { value: engine.snapshot().input_devices[0]?.id },
    });
    await advance(20);
    expect(engine.snapshot().selected_input_id).toBe(engine.snapshot().input_devices[0]?.id);
    expect(engine.snapshot().send_muted).toBe(true);
    expect(sendMute().getAttribute("aria-pressed")).toBe("true");
  });

  it("送信ミュートの解除は明示的な操作で行い、表示は ACK のあとに変わる", async () => {
    fireEvent.click(sendMute());
    expect(sendMute().getAttribute("aria-pressed")).toBe("true");
    await advance(20);
    expect(sendMute().getAttribute("aria-pressed")).toBe("false");
    expect(sendMute().textContent).toBe("送信中");
  });

  it("ステレオに切り替えても送信ミュートのまま", async () => {
    const channels = screen.getByRole("combobox", { name: "チャンネル" }) as HTMLSelectElement;
    expect(channels.value).toBe("1");
    fireEvent.change(channels, { target: { value: "2" } });
    await advance(20);
    expect(engine.snapshot().input_channels).toBe(2);
    expect(channels.value).toBe("2");
    expect(engine.snapshot().send_muted).toBe(true);
  });

  it("「入力なし」を選べる", () => {
    expect(screen.getByRole("option", { name: "入力なし" })).toBeTruthy();
  });
});

describe("参加の流れ", () => {
  it("起動時の招待は入力欄に入れるだけで、自動では参加しない", async () => {
    await advance(5000);
    expect(engine.snapshot().client_state).toBe("DISCONNECTED");
    expect((screen.getByRole("textbox") as HTMLInputElement).value).toBe("7KQ2-M9XD");
  });

  it("招待を入力して参加を申請すると、承認待ちを経て接続する", async () => {
    fireEvent.click(screen.getByRole("button", { name: "参加を申請" }));
    await advance(1100);
    expect(screen.getByText("承認待ち")).toBeTruthy();
    await advance(4000);
    expect(screen.getByText("接続済み")).toBeTruthy();
    expect(screen.getByRole("button", { name: "退出" })).toBeTruthy();
  });

  it("失敗は原因別の文言で表示する", async () => {
    fireEvent.input(screen.getByRole("textbox"), { target: { value: "expired-code" } });
    fireEvent.click(screen.getByRole("button", { name: "参加を申請" }));
    await advance(1000);
    expect(screen.getByRole("alert").textContent).toBe("招待の有効期限が切れています");
  });
});

describe("開発中のボタン（AT-A09）", () => {
  it("押してもトーストを出すだけで、ウィンドウを開かず、エンジンにコマンドを送らない", async () => {
    const open = vi.spyOn(window, "open").mockImplementation(() => null);
    const send = vi.spyOn(engine, "send");
    const before = JSON.stringify(engine.snapshot());
    const buttons = Array.from(document.querySelectorAll<HTMLButtonElement>("[data-planned]"));
    expect(buttons.length).toBeGreaterThan(0);
    for (const button of buttons) fireEvent.click(button);
    await advance(20);
    expect(open).not.toHaveBeenCalled();
    expect(send).not.toHaveBeenCalled();
    expect(JSON.stringify(engine.snapshot())).toBe(before);
    expect(screen.queryByRole("dialog")).toBeNull();
    expect(toasts.get().history.map((t) => t.text)).toEqual([
      "『返送モニター』は現在開発中です。今後のアップデートで利用可能になる予定です。",
      "『アプリ音声』は現在開発中です。今後のアップデートで利用可能になる予定です。",
    ]);
  });
});
