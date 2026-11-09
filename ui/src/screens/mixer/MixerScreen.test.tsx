// SPDX-License-Identifier: AGPL-3.0-only
// ミキサー画面の受け入れ。
// AT-A04 の UI 部分: 子機側と親機側のミュートの理由を区別して表示し、-∞ dB とミュートを区別する。
//   ミュートの表示は ACK のあとに変わり、ミュートを繰り返しても NO SIGNAL と誤表示しない。
// AT-A09 の UI 部分: 開発中のボタンはトーストを出すだけで、ウィンドウも状態も変えない。
import { act, fireEvent, render, screen, within } from "@testing-library/preact";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { EngineClient } from "../../ipc/engine-client";
import { FakeMixerEngine } from "../../ipc/fake-engine/fake-mixer-engine";
import { LinkStatsStore } from "../../state/link-stats-store";
import { MeterStore } from "../../state/meter-store";
import { MixerStore } from "../../state/mixer-store";
import { OptimisticStore } from "../../state/optimistic-store";
import { ToastStore } from "../../state/toast-store";
import { FakeTimers } from "../../utils/fake-timers";
import { connectContinuousFeedback } from "../shared/feedback";
import { MixerScreen } from "./MixerScreen";

let timers: FakeTimers;
let engine: FakeMixerEngine;
let toasts: ToastStore;

/** 時刻を進め、ACK を待つ Promise の解決と再描画まで済ませる。 */
async function advance(ms: number) {
  await act(async () => timers.advance(ms));
}

function strip(name: string): HTMLElement {
  return screen.getByRole("region", { name: `${name} のチャンネル` });
}

beforeEach(async () => {
  timers = new FakeTimers();
  engine = new FakeMixerEngine({ timers, meterIntervalMs: 0, latencyMs: 10 });
  const client = new EngineClient(engine, timers);
  const store = new MixerStore();
  const optimistic = new OptimisticStore();
  toasts = new ToastStore(timers, () => 0);
  const linkStats = new LinkStatsStore();
  client.subscribe((m) => {
    store.apply(m);
    if (m.type === "link_stats") linkStats.apply(m);
  });
  connectContinuousFeedback(client, optimistic, toasts);
  render(
    <MixerScreen
      client={client}
      store={store}
      optimistic={optimistic}
      meters={new MeterStore(timers)}
      linkStats={linkStats}
      toasts={toasts}
      onOpenRouting={() => {}}
      writeClipboard={() => Promise.resolve()}
    />,
  );
  client.hello();
  await advance(20);
});

afterEach(() => vi.restoreAllMocks());

describe("ミュートの表示（AT-A04）", () => {
  it("子機側の送信ミュートと親機側のチャンネルミュートを、ラベルで区別して表示する", async () => {
    // Gt 佐藤: 子機も親機もミュート。Vo 山田: 親機だけミュート。
    const gt = strip("Gt 佐藤");
    expect(within(gt).getByText("子機MUTE").closest(".mute-badge")?.classList).toContain(
      "mute-badge--on",
    );
    expect(
      within(gt).getByRole("button", { name: "Gt 佐藤 のミュート" }).getAttribute("aria-pressed"),
    ).toBe("true");

    const vo = strip("Vo 山田");
    expect(within(vo).getByText("子機MUTE").closest(".mute-badge")?.classList).not.toContain(
      "mute-badge--on",
    );
    expect(
      within(vo).getByRole("button", { name: "Vo 山田 のミュート" }).getAttribute("aria-pressed"),
    ).toBe("true");
  });

  it("子機側の送信ミュートは親機から操作できない（ボタンにしない）", async () => {
    const gt = strip("Gt 佐藤");
    expect(within(gt).queryByRole("button", { name: /子機MUTE/ })).toBeNull();
  });

  it("-∞ dB とミュートを区別して表示する", async () => {
    // Cho 伊藤は -∞ dB。チャンネルミュートを解除しても -∞ dB の表示のままで、MUTE は消える。
    const cho = strip("Cho 伊藤");
    const mute = within(cho).getByRole("button", { name: "Cho 伊藤 のミュート" });
    fireEvent.click(mute);
    await advance(20);
    expect(mute.getAttribute("aria-pressed")).toBe("false");
    expect(within(cho).getByText("-∞")).toBeTruthy();
  });

  it("ミュートの表示は ACK のあとに変わる", async () => {
    const vo = strip("Vo 山田");
    const mute = within(vo).getByRole("button", { name: "Vo 山田 のミュート" });
    fireEvent.click(mute);
    expect(mute.getAttribute("aria-pressed")).toBe("true");
    expect(mute.getAttribute("aria-busy")).toBe("true");
    await advance(20);
    expect(mute.getAttribute("aria-pressed")).toBe("false");
    expect(mute.getAttribute("aria-busy")).toBe("false");
  });

  it("ミュートと解除を繰り返しても NO SIGNAL と表示しない", async () => {
    const vo = strip("Vo 山田");
    const mute = within(vo).getByRole("button", { name: "Vo 山田 のミュート" });
    for (let i = 0; i < 6; i++) {
      fireEvent.click(mute);
      await advance(20);
    }
    expect(within(vo).queryByText("NO SIGNAL")).toBeNull();
  });

  it("エンジンの安全ミュートを SAFETY MUTE として区別し、出力デバイスの消失をステータス帯に残す", async () => {
    act(() => engine.simulateOutputLost());
    const master = screen.getByRole("region", { name: "Master" });
    expect(within(master).getByRole("button", { name: "Master のミュート" }).textContent).toBe(
      "SAFETY MUTE",
    );
    expect(screen.getByText("出力デバイス消失")).toBeTruthy();
    await advance(10_000);
    expect(screen.getByText("出力デバイス消失")).toBeTruthy();
  });
});

describe("出力デバイスの選択", () => {
  function outputSelect(): HTMLSelectElement {
    return screen.getByRole("combobox", { name: "出力デバイスを選ぶ" });
  }

  it("選んだデバイスは ACK のあとに表示し、Master のミュートは変えない", async () => {
    expect(outputSelect().value).toBe("usb");
    fireEvent.change(outputSelect(), { target: { value: "onboard" } });
    await advance(20);
    expect(engine.snapshot().session.output_device_id).toBe("onboard");
    expect(outputSelect().value).toBe("onboard");
    expect(engine.snapshot().master.master_muted).toBe(true);
  });

  it("開けないデバイスはトーストで知らせ、出力なしと表示する", async () => {
    fireEvent.change(outputSelect(), { target: { value: "broken" } });
    await advance(20);
    expect(screen.getByText(/出力デバイスを開けません/)).toBeTruthy();
    expect(outputSelect().value).toBe("");
  });

  it("消失したあとは、見つからないデバイスを選択中として示し、選び直せる", async () => {
    act(() => engine.simulateOutputLost());
    expect(outputSelect().selectedOptions[0]?.textContent).toBe("（見つからないデバイス）");
    fireEvent.change(outputSelect(), { target: { value: "usb" } });
    await advance(20);
    expect(engine.snapshot().session.output_device_state).toBe("OPEN");
    expect(screen.queryByText("出力デバイス消失")).toBeNull();
  });
});

describe("フェーダーの楽観的更新", () => {
  it("ドラッグ中は手元の値を表示し、ACK が 500 ms 来なければ戻して警告する", async () => {
    // エンジンに届かない状況を再現するため、連続値のコマンドだけを捨てる。
    const deliver = engine.send.bind(engine);
    vi.spyOn(engine, "send").mockImplementation((m) => {
      if (m.type !== "set_gain") deliver(m);
    });
    const vo = strip("Vo 山田");
    const slider = within(vo).getByRole("slider");
    fireEvent.pointerDown(slider, { pointerId: 1, button: 0, clientY: 100 });
    fireEvent.pointerMove(slider, { pointerId: 1, clientY: 80 });
    expect(within(vo).getByText("+4.0")).toBeTruthy();
    await advance(500);
    expect(within(vo).getByText("0.0")).toBeTruthy();
    expect(toasts.get().visible.some((t) => t.severity === "warning")).toBe(true);
  });

  it("ACK が届いたら操作の終了後にエンジンの値の表示に戻す", async () => {
    const vo = strip("Vo 山田");
    const slider = within(vo).getByRole("slider");
    fireEvent.pointerDown(slider, { pointerId: 1, button: 0, clientY: 100 });
    fireEvent.pointerMove(slider, { pointerId: 1, clientY: 80 });
    fireEvent.pointerUp(slider, { pointerId: 1, clientY: 80 });
    await advance(50);
    expect(within(vo).getByText("+4.0")).toBeTruthy();
    expect(engine.snapshot().channels[0]?.gain_db).toBe(4);
    expect(toasts.get().history).toHaveLength(0);
  });
});

describe("開発中のボタン（AT-A09）", () => {
  it("押してもトーストを出すだけで、ウィンドウを開かず、エンジンにコマンドを送らない", async () => {
    const open = vi.spyOn(window, "open").mockImplementation(() => null);
    const send = vi.spyOn(engine, "send");
    const before = JSON.stringify(engine.snapshot());
    const buttons = Array.from(document.querySelectorAll<HTMLButtonElement>("[data-planned]"));
    // ツールバーの 5 機能と、各ストリップの FX。
    expect(buttons.length).toBe(5 + engine.snapshot().channels.length);
    for (const button of buttons) fireEvent.click(button);
    await advance(20);
    expect(open).not.toHaveBeenCalled();
    expect(send).not.toHaveBeenCalled();
    expect(JSON.stringify(engine.snapshot())).toBe(before);
    expect(screen.queryByRole("dialog")).toBeNull();
    expect(screen.queryByRole("alertdialog")).toBeNull();
    const history = toasts.get().history;
    expect(history).toHaveLength(buttons.length);
    expect(history.every((t) => t.severity === "info" && t.text.includes("開発中"))).toBe(true);
    expect(toasts.get().visible.length).toBeLessThanOrEqual(3);
  });
});

describe("セッションの終了", () => {
  it("取り消せない操作なので確認ダイアログを出し、既定のフォーカスをキャンセルに置く", async () => {
    fireEvent.click(screen.getByRole("button", { name: "セッションを終了" }));
    const dialog = screen.getByRole("alertdialog");
    expect(document.activeElement?.textContent).toBe("キャンセル");
    fireEvent.click(within(dialog).getByRole("button", { name: "キャンセル" }));
    await advance(20);
    expect(engine.snapshot().session.state).toBe("ACTIVE");
  });
});

describe("参加の承認", () => {
  it("非モーダルのパネルで承認でき、承認した参加者のストリップはミュートで始まる", async () => {
    fireEvent.click(screen.getByRole("button", { name: /参加申請/ }));
    const panel = screen.getByRole("complementary", { name: "参加申請" });
    fireEvent.click(within(panel).getByRole("button", { name: "承認" }));
    await advance(20);
    const tp = strip("Tp 渡辺");
    expect(
      within(tp).getByRole("button", { name: "Tp 渡辺 のミュート" }).getAttribute("aria-pressed"),
    ).toBe("true");
    expect(within(tp).getByText("子機MUTE").closest(".mute-badge")?.classList).toContain(
      "mute-badge--on",
    );
  });
});

describe("目標遅延と受信の実測値", () => {
  it("目標遅延の選択はエンジンに送り、ACK のあとの状態で表示する", async () => {
    const select = screen.getByRole("combobox", { name: "全チャンネルの目標遅延" });
    expect((select as HTMLSelectElement).value).toBe("20");
    fireEvent.change(select, { target: { value: "40" } });
    await advance(20);
    expect(engine.snapshot().session.latency_target_ms).toBe(40);
    expect((select as HTMLSelectElement).value).toBe("40");
  });

  it("受信しているチャンネルに揺らぎとバッファの長さを示し、値のないチャンネルは記号で示す", async () => {
    await act(async () => engine.sendLinkStats());
    const vo = strip("Vo 山田");
    expect(within(vo).getByText("揺らぎ").nextElementSibling?.textContent).toMatch(
      /^\d+(\.\d)? ms$/,
    );
    const gt = strip("Gt 佐藤");
    expect(within(gt).getByText("揺らぎ").nextElementSibling?.textContent).toBe("—");
  });
});
