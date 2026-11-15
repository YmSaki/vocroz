// SPDX-License-Identifier: AGPL-3.0-only
// ルーティング画面。編集はステージングとしてエンジンへ送り、表示はエンジンから戻った構成と検証の結果で
// 変わる。音が変わるのは適用を押したときだけで、遅延ノードを通るループは承認しなければ適用できない。
import { act, fireEvent, render, screen, within } from "@testing-library/preact";
import { beforeEach, describe, expect, it, vi } from "vitest";
import { EngineClient } from "../../ipc/engine-client";
import { FakeMixerEngine } from "../../ipc/fake-engine/fake-mixer-engine";
import type { RoutingConfig } from "../../ipc/messages";
import { MixerStore } from "../../state/mixer-store";
import { OptimisticStore } from "../../state/optimistic-store";
import { PluginStore } from "../../state/plugin-store";
import { ToastStore } from "../../state/toast-store";
import { FakeTimers } from "../../utils/fake-timers";
import { RoutingScreen } from "./RoutingScreen";

let timers: FakeTimers;
let engine: FakeMixerEngine;
let client: EngineClient;
let closed: number;
let toasts: ToastStore;

/** 時刻を進め、ACK を待つ Promise の解決と再描画まで済ませる。 */
async function advance(ms: number) {
  await act(async () => timers.advance(ms));
}

/** エンジンへ直接ステージングする（画面の操作を何段も重ねずに状態を作るため）。 */
async function stage(config: RoutingConfig) {
  void client.sendCommand("routing", engine.snapshot().routing.revision, {
    type: "stage_routing",
    config,
  });
  await advance(20);
}

function echoConfig(): RoutingConfig {
  return {
    buses: [{ entity_id: "bus-fx", name: "FX", kind: "USER" }],
    outputs: [],
    delays: [{ entity_id: "delay-echo", delay_ms: 250 }],
    sends: [
      { entity_id: "send-1", from: "master", to: "delay-echo", tap: "POST_FADER" },
      { entity_id: "send-2", from: "delay-echo", to: "bus-fx", tap: "POST_FADER" },
      { entity_id: "send-3", from: "bus-fx", to: "master", tap: "POST_FADER" },
    ],
    input_delays: [],
    inserts: [],
  };
}

const applyButton = () => screen.getByRole("button", { name: "適用" });

beforeEach(async () => {
  timers = new FakeTimers();
  engine = new FakeMixerEngine({ timers, meterIntervalMs: 0, latencyMs: 10 });
  client = new EngineClient(engine, timers);
  const store = new MixerStore();
  const plugins = new PluginStore();
  client.subscribe((m) => {
    store.apply(m);
    plugins.apply(m);
  });
  closed = 0;
  toasts = new ToastStore(timers, () => 0);
  render(
    <RoutingScreen
      client={client}
      store={store}
      plugins={plugins}
      optimistic={new OptimisticStore()}
      toasts={toasts}
      onClose={() => (closed += 1)}
    />,
  );
  client.hello();
  await advance(20);
});

describe("ステージングと適用", () => {
  it("編集はステージングするだけで、適用を押すまで適用中の構成を変えない", async () => {
    expect(applyButton()).toHaveProperty("disabled", true);
    fireEvent.click(screen.getByRole("button", { name: "バスを追加" }));
    await advance(20);
    const routing = engine.snapshot().routing;
    expect(routing.staged?.config.buses).toEqual([
      { entity_id: "bus-1", name: "バス 1", kind: "USER" },
    ]);
    expect(routing.applied.buses).toEqual([]);
    expect(screen.getByRole("status").textContent).toContain("未適用の変更があります");

    fireEvent.click(applyButton());
    await advance(20);
    expect(engine.snapshot().routing.applied.buses).toHaveLength(1);
    // 新しいバスはミュートで始まる（初期ミュート）。
    const level = screen.getByRole("group", { name: "バス 1" });
    expect(within(level).getByRole("button", { name: "バス 1 のミュート" })).toHaveProperty(
      "ariaPressed",
      "true",
    );
  });

  it("検証の問題を、理由と原因の項目で示し、適用させない", async () => {
    await stage({
      buses: [],
      outputs: [{ entity_id: "out-1", name: "IEM", device_channel: 0 }],
      sends: [],
      delays: [],
      input_delays: [],
      inserts: [],
    });
    expect(screen.getByRole("status").textContent).toBe(
      "出力のチャンネルがメインの出力かほかの出力と重なっています（IEM）",
    );
    const row = document.querySelector('[data-entity-id="out-1"]');
    expect(row?.classList).toContain("routing-row--issue");
    expect(applyButton()).toHaveProperty("disabled", true);
  });

  it("遅延ノードを通るループは、承認したときだけ適用できる", async () => {
    await stage(echoConfig());
    expect(screen.getByRole("status").textContent).toContain("ループの承認が必要です");
    expect(applyButton()).toHaveProperty("disabled", true);
    fireEvent.click(screen.getByRole("checkbox", { name: "ループを承認する" }));
    expect(applyButton()).toHaveProperty("disabled", false);
    fireEvent.click(applyButton());
    await advance(20);
    const routing = engine.snapshot().routing;
    expect(routing.applied).toEqual(echoConfig());
    expect(routing.feedback_approved).toBe(true);
  });

  it("ステージングし直したら、承認をやり直す", async () => {
    await stage(echoConfig());
    fireEvent.click(screen.getByRole("checkbox", { name: "ループを承認する" }));
    await stage({ ...echoConfig(), delays: [{ entity_id: "delay-echo", delay_ms: 300 }] });
    expect(screen.getByRole("checkbox", { name: "ループを承認する" })).toHaveProperty(
      "checked",
      false,
    );
    expect(applyButton()).toHaveProperty("disabled", true);
  });

  it("破棄すると、適用中の構成の表示に戻る", async () => {
    fireEvent.click(screen.getByRole("button", { name: "バスを追加" }));
    await advance(20);
    fireEvent.click(screen.getByRole("button", { name: "変更を破棄" }));
    await advance(20);
    expect(engine.snapshot().routing.staged).toBeNull();
    expect(screen.queryByRole("textbox", { name: "バス 1 の名前" })).toBeNull();
  });

  it("バスを消すと、そのバスを端点とするセンドも一緒に消す", async () => {
    await stage(echoConfig());
    fireEvent.click(screen.getByRole("button", { name: "FX を削除" }));
    await advance(20);
    const sends = engine.snapshot().routing.staged?.config.sends.map((s) => s.entity_id);
    expect(sends).toEqual(["send-1"]);
  });
});

describe("バスとセンドのレベル", () => {
  it("ミュートの解除は ACK のあとに表示が変わる", async () => {
    await stage(echoConfig());
    fireEvent.click(screen.getByRole("checkbox", { name: "ループを承認する" }));
    fireEvent.click(applyButton());
    await advance(20);
    const mute = () => screen.getByRole("button", { name: "FX のミュート" });
    fireEvent.click(mute());
    expect(mute()).toHaveProperty("ariaPressed", "true");
    await advance(20);
    expect(mute()).toHaveProperty("ariaPressed", "false");
    expect(engine.snapshot().routing.levels.find((l) => l.entity_id === "bus-fx")?.muted).toBe(
      false,
    );
  });
});

it("ミキサーに戻るボタンで画面を閉じる", () => {
  fireEvent.click(screen.getByRole("button", { name: "ミキサーに戻る" }));
  expect(closed).toBe(1);
});

describe("プラグインの挿入", () => {
  const insertsSection = () => screen.getByRole("region", { name: "プラグインの挿入" });

  it("挿入はステージングするだけで、適用すると読み込みの状態とエディターのボタンが出る", async () => {
    fireEvent.click(within(insertsSection()).getByRole("button", { name: "挿入を追加" }));
    await advance(20);
    const routing = engine.snapshot().routing;
    expect(routing.staged?.config.inserts).toEqual([
      { entity_id: "fx-1", target: "master", plugin_id: "VST3-DemoEQ" },
    ]);
    expect(routing.applied.inserts).toEqual([]);
    expect(within(insertsSection()).getByText("適用すると読み込みます")).toBeTruthy();
    expect(
      within(insertsSection()).getByRole("button", { name: "Demo EQ のエディターを開く" }),
    ).toHaveProperty("disabled", true);

    fireEvent.click(applyButton());
    await advance(20);
    expect(within(insertsSection()).getByText("使用中")).toBeTruthy();
    expect(
      within(insertsSection()).getByRole("button", { name: "Demo EQ のエディターを開く" }),
    ).toHaveProperty("disabled", false);
  });

  it("VSTi は挿せるプラグインの選択肢に出さない", async () => {
    fireEvent.click(within(insertsSection()).getByRole("button", { name: "挿入を追加" }));
    await advance(20);
    const select = within(insertsSection()).getByRole("combobox", {
      name: "Master に挿すプラグイン",
    });
    const names = Array.from(select.querySelectorAll("option")).map((o) => o.textContent);
    expect(names).toEqual(["Demo EQ", "Demo Compressor", "Demo Reverb"]);
  });

  it("読み込みの済んだ挿入のエディターを開くコマンドを送る", async () => {
    await stage({
      ...echoConfigWithoutLoop(),
      inserts: [{ entity_id: "fx-1", target: "master", plugin_id: "VST3-DemoComp" }],
    });
    fireEvent.click(applyButton());
    await advance(20);
    const send = vi.spyOn(engine, "send");
    fireEvent.click(
      within(insertsSection()).getByRole("button", { name: "Demo Compressor のエディターを開く" }),
    );
    await advance(20);
    expect(send).toHaveBeenCalledWith(
      expect.objectContaining({
        type: "open_plugin_editor",
        entity_id: "plugins",
        insert_id: "fx-1",
      }),
    );
  });

  it("失敗した挿入は理由を出し、読み込み直すと新しい挿入としてステージングする", async () => {
    await stage({
      ...echoConfigWithoutLoop(),
      inserts: [{ entity_id: "fx-1", target: "master", plugin_id: "VST3-DemoComp" }],
    });
    fireEvent.click(applyButton());
    await advance(20);
    act(() => engine.simulatePluginFailure("fx-1"));
    await advance(20);
    expect(within(insertsSection()).getByText("処理中に止まりました（無音）")).toBeTruthy();
    fireEvent.click(
      within(insertsSection()).getByRole("button", { name: "Demo Compressor を読み込み直す" }),
    );
    await advance(20);
    expect(engine.snapshot().routing.staged?.config.inserts).toEqual([
      { entity_id: "fx-2", target: "master", plugin_id: "VST3-DemoComp" },
    ]);
  });
});

describe("プラグインのスキャン", () => {
  it("再スキャンを押すとすぐにスキャン中になり、終わると使えるプラグインの数を出す", async () => {
    const section = screen.getByRole("region", { name: "プラグインのスキャン" });
    expect(within(section).getByText("使えるプラグイン: 4 個")).toBeTruthy();
    fireEvent.click(within(section).getByRole("button", { name: "再スキャン" }));
    await advance(20);
    expect(within(section).getByText("スキャンしています…")).toBeTruthy();
    expect(within(section).getByRole("button", { name: "再スキャン" })).toHaveProperty(
      "disabled",
      true,
    );
    await advance(2000);
    expect(within(section).getByText("使えるプラグイン: 4 個")).toBeTruthy();
  });
});

/** ループのない構成（挿入の試験の土台）。 */
function echoConfigWithoutLoop(): RoutingConfig {
  return { buses: [], outputs: [], sends: [], delays: [], input_delays: [], inserts: [] };
}
