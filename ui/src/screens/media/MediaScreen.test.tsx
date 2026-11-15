// SPDX-License-Identifier: AGPL-3.0-only
// メディア画面。録音は録る対象を選んでから始められ、録音中は設定を変えられない。失敗と欠落は画面に示し、
// ライブの音声は止めない。プレイヤーは一覧のファイルだけを読み込み、読み込んだだけでは鳴らさない
// （初期ミュート）。表示はエンジンから届いた状態で変わる（エンジン主権）。
import { act, fireEvent, render, screen } from "@testing-library/preact";
import { beforeEach, describe, expect, it } from "vitest";
import { EngineClient } from "../../ipc/engine-client";
import { FakeMixerEngine } from "../../ipc/fake-engine/fake-mixer-engine";
import { MixerStore } from "../../state/mixer-store";
import { ToastStore } from "../../state/toast-store";
import { FakeTimers } from "../../utils/fake-timers";
import { connectNotices } from "../shared/feedback";
import { MediaScreen } from "./MediaScreen";

let timers: FakeTimers;
let engine: FakeMixerEngine;
let client: EngineClient;
let toasts: ToastStore;
let closed: number;

/** 時刻を進め、ACK を待つ Promise の解決と再描画まで済ませる。 */
async function advance(ms: number) {
  await act(async () => timers.advance(ms));
}

const button = (name: string) => screen.getByRole<HTMLButtonElement>("button", { name });
const checkbox = (name: string) => screen.getByRole<HTMLInputElement>("checkbox", { name });

beforeEach(async () => {
  timers = new FakeTimers();
  // 経過時間と再生位置はメーターの周期で進むので、メーターを回す。
  engine = new FakeMixerEngine({ timers, meterIntervalMs: 50, latencyMs: 10, random: () => 0 });
  client = new EngineClient(engine, timers);
  const store = new MixerStore();
  toasts = new ToastStore(timers, () => 0);
  client.subscribe((m) => store.apply(m));
  connectNotices(client, toasts);
  closed = 0;
  render(
    <MediaScreen
      client={client}
      store={store}
      toasts={toasts}
      onClose={() => (closed += 1)}
      inOwnWindow
    />,
  );
  client.hello();
  await advance(20);
});

describe("録音", () => {
  it("起動時は録音しておらず、選んだ対象のファイルに分けて録り、止めるとテイクが一覧に加わる", async () => {
    expect(button("録音開始").ariaPressed).toBe("false");
    expect(checkbox("Master").checked).toBe(true);
    fireEvent.click(checkbox("Vo 山田"));
    await advance(20);
    expect(engine.snapshot().recorder.config.sources).toEqual(["master", "ch1"]);

    fireEvent.click(button("録音開始"));
    await advance(20);
    expect(engine.snapshot().recorder.state).toBe("RECORDING");
    expect(screen.getByText("01-Master.wav")).toBeTruthy();
    expect(screen.getByText("02-Vo 山田.wav")).toBeTruthy();

    await advance(2000);
    expect(screen.getByRole("timer", { name: "経過時間" }).textContent).toBe("0:02");

    fireEvent.click(button("録音停止"));
    await advance(20);
    expect(engine.snapshot().recorder.state).toBe("STOPPED");
    const take = engine.snapshot().recorder.take ?? "";
    expect(screen.getByRole("button", { name: `${take}/01-Master.wav` })).toBeTruthy();
  });

  it("録音中は形式、保存先、録る対象を変えられない", async () => {
    fireEvent.click(button("録音開始"));
    await advance(20);
    expect(
      screen.getByRole<HTMLInputElement>("radio", { name: "FLAC（48 kHz / 24 bit）" }).disabled,
    ).toBe(true);
    expect(screen.getByRole<HTMLInputElement>("textbox", { name: "保存先フォルダ" }).disabled).toBe(
      true,
    );
    expect(checkbox("Master").closest("fieldset")?.disabled).toBe(true);
  });

  it("録る対象がなければ開始できない", async () => {
    fireEvent.click(checkbox("Master"));
    await advance(20);
    expect(button("録音開始").disabled).toBe(true);
  });

  it("空き容量が足りずに始められなかったことを示す", async () => {
    engine.simulateDiskFullOnStart();
    fireEvent.click(button("録音開始"));
    await advance(20);
    expect(engine.snapshot().recorder.state).toBe("STOPPED");
    expect(screen.getByRole("alert").textContent).toContain("空き容量が足りない");
  });

  it("録音中に書けなくなったら止まったことを示し、通知でライブの音声は続くと知らせる", async () => {
    fireEvent.click(button("録音開始"));
    await advance(20);
    await act(async () => engine.simulateRecordingFailed());
    await advance(20);
    expect(button("録音開始")).toBeTruthy();
    expect(screen.getAllByRole("alert").some((e) => e.textContent?.includes("空き容量"))).toBe(
      true,
    );
    expect(toasts.get().history.some((t) => t.text.includes("ライブの音声は流れ続けて"))).toBe(
      true,
    );
  });

  it("書き込みの遅れで無音を埋めたトラックを警告する", async () => {
    fireEvent.click(button("録音開始"));
    await advance(20);
    await act(async () => engine.simulateDroppedFrames(12));
    await advance(20);
    expect(screen.getByRole("alert").textContent).toContain("12 ms を無音で埋めました");
  });
});

describe("プレイヤー", () => {
  it("読み込んだだけでは鳴らさず、再生で位置が進み、停止で先頭に戻る", async () => {
    expect(button("再生").disabled).toBe(true);
    fireEvent.click(button("bgm/opening.flac"));
    await advance(20);
    expect(engine.snapshot().media_player.file).toBe("bgm/opening.flac");
    expect(engine.snapshot().media_player.transport).toBe("STOPPED");

    fireEvent.click(button("再生"));
    await advance(1000);
    expect(engine.snapshot().media_player.position_ms).toBeGreaterThan(0);
    expect(screen.getByText(/^0:0[01] \/ 3:00$/)).toBeTruthy();

    fireEvent.click(button("停止"));
    await advance(20);
    expect(engine.snapshot().media_player.transport).toBe("STOPPED");
    expect(engine.snapshot().media_player.position_ms).toBe(0);
  });

  it("シークバーを離したときに位置を送る", async () => {
    fireEvent.click(button("bgm/opening.flac"));
    await advance(20);
    const seek = screen.getByRole<HTMLInputElement>("slider", { name: "再生位置" });
    fireEvent.input(seek, { target: { value: "60000" } });
    // ドラッグ中はまだ送らない。
    expect(engine.snapshot().media_player.position_ms).toBe(0);
    fireEvent.change(seek, { target: { value: "60000" } });
    await advance(20);
    expect(engine.snapshot().media_player.position_ms).toBe(60000);
  });

  it("流す先は Master と適用中のバスから選ぶ", async () => {
    void client.sendCommand("routing", engine.snapshot().routing.revision, {
      type: "stage_routing",
      config: {
        buses: [{ entity_id: "bus-bgm", name: "BGM", kind: "USER" }],
        outputs: [],
        sends: [],
        delays: [],
        input_delays: [],
        inserts: [],
      },
    });
    await advance(20);
    void client.sendCommand("routing", engine.snapshot().routing.revision, {
      type: "commit_routing",
      approve_feedback: false,
    });
    await advance(20);
    const target = screen.getByRole<HTMLSelectElement>("combobox", { name: "流す先" });
    expect(Array.from(target.options).map((o) => o.textContent)).toEqual(["Master", "BGM"]);
    fireEvent.change(target, { target: { value: "bus-bgm" } });
    await advance(20);
    expect(engine.snapshot().media_player.target).toBe("bus-bgm");
  });
});

it("閉じるボタンで窓を閉じる", () => {
  fireEvent.click(button("閉じる"));
  expect(closed).toBe(1);
});
