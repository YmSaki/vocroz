// SPDX-License-Identifier: AGPL-3.0-only
// フェーダーの操作の受け入れ（AT-A04 の UI 部分）:
// ドラッグ、数値入力、キーボードで値が変わり、ホイールでは変わらないこと。
import { fireEvent, render, screen } from "@testing-library/preact";
import { describe, expect, it, vi } from "vitest";
import type { GainDb } from "../../utils/db";
import { Fader } from "./Fader";
import { kMixerFaderScale } from "./fader-scale";

function setup(value: GainDb = 0) {
  const onInput = vi.fn();
  const onCommit = vi.fn();
  render(
    <Fader
      value={value}
      scale={kMixerFaderScale}
      label="Vo"
      onInput={onInput}
      onCommit={onCommit}
    />,
  );
  return { onInput, onCommit, slider: screen.getByRole("slider") };
}

// 高さを測れない jsdom では、つまみの移動範囲は 200 px になる。0 dB は 3/4 の高さで、
// その上の 1/4（50 px）が +10 dB までに当たる。上へ 20 px のドラッグは +4 dB。
describe("Fader のドラッグ", () => {
  it("ドラッグで値を変え、離したときに確定する", () => {
    const { onInput, onCommit, slider } = setup(0);
    fireEvent.pointerDown(slider, { pointerId: 1, button: 0, clientY: 100 });
    fireEvent.pointerMove(slider, { pointerId: 1, clientY: 80 });
    expect(onInput).toHaveBeenLastCalledWith(4);
    fireEvent.pointerUp(slider, { pointerId: 1, clientY: 80 });
    expect(onCommit).toHaveBeenCalledWith(4);
  });

  it("Ctrl を押しながらのドラッグは 1/10 の感度で、0 dB に吸着しない", () => {
    const { onInput, slider } = setup(0);
    fireEvent.pointerDown(slider, { pointerId: 1, button: 0, clientY: 100, ctrlKey: true });
    fireEvent.pointerMove(slider, { pointerId: 1, clientY: 80, ctrlKey: true });
    expect(onInput).toHaveBeenLastCalledWith(0.4);
  });

  it("通常のドラッグでは 0 dB の近くで 0 dB に吸着する", () => {
    const { onInput, slider } = setup(-3);
    const start = kMixerFaderScale.toPosition(-3);
    const pxTo0 = (kMixerFaderScale.toPosition(0) - start) * 200;
    fireEvent.pointerDown(slider, { pointerId: 1, button: 0, clientY: 100 });
    fireEvent.pointerMove(slider, { pointerId: 1, clientY: 100 - pxTo0 - 1 });
    expect(onInput).toHaveBeenLastCalledWith(0);
  });

  it("一番下までドラッグすると -∞ dB になる", () => {
    const { onInput, slider } = setup(0);
    fireEvent.pointerDown(slider, { pointerId: 1, button: 0, clientY: 100 });
    fireEvent.pointerMove(slider, { pointerId: 1, clientY: 400 });
    expect(onInput).toHaveBeenLastCalledWith(null);
  });
});

describe("Fader のホイール", () => {
  it("マウスホイールでは値を変えない", () => {
    const { onInput, onCommit, slider } = setup(0);
    fireEvent.wheel(slider, { deltaY: -100 });
    fireEvent.wheel(slider, { deltaY: 100 });
    expect(onInput).not.toHaveBeenCalled();
    expect(onCommit).not.toHaveBeenCalled();
  });
});

describe("Fader のキーボード", () => {
  it("矢印キーで 0.5 dB、Shift + 矢印キーで 0.1 dB ずつ動かす", () => {
    const { onCommit, slider } = setup(0);
    fireEvent.keyDown(slider, { key: "ArrowUp" });
    expect(onCommit).toHaveBeenLastCalledWith(0.5);
    fireEvent.keyDown(slider, { key: "ArrowDown", shiftKey: true });
    expect(onCommit).toHaveBeenLastCalledWith(-0.1);
  });

  it("有限の最小値から下げると -∞、-∞ から上げると有限の最小値にする", () => {
    const low = setup(-60);
    fireEvent.keyDown(low.slider, { key: "ArrowDown" });
    expect(low.onCommit).toHaveBeenLastCalledWith(null);
  });

  it("-∞ から上げると有限の最小値にする", () => {
    const { onCommit, slider } = setup(null);
    fireEvent.keyDown(slider, { key: "ArrowUp" });
    expect(onCommit).toHaveBeenLastCalledWith(-60);
  });

  it("最大値より上には動かさない", () => {
    const { onCommit, slider } = setup(10);
    fireEvent.keyDown(slider, { key: "ArrowUp" });
    expect(onCommit).not.toHaveBeenCalled();
  });
});

describe("Fader の数値入力", () => {
  it("数値をダブルクリックして入力し、Enter で確定する", () => {
    const { onCommit } = setup(0);
    fireEvent.dblClick(screen.getByRole("button"));
    const input = screen.getByRole("textbox");
    fireEvent.input(input, { target: { value: "-6.5" } });
    fireEvent.keyDown(input, { key: "Enter" });
    expect(onCommit).toHaveBeenCalledWith(-6.5);
  });

  it("-inf と入力すると -∞ dB にする", () => {
    const { onCommit } = setup(0);
    fireEvent.dblClick(screen.getByRole("button"));
    const input = screen.getByRole("textbox");
    fireEvent.input(input, { target: { value: "-inf" } });
    fireEvent.keyDown(input, { key: "Enter" });
    expect(onCommit).toHaveBeenCalledWith(null);
  });

  it("Escape と読み取れない入力では値を変えない", () => {
    const { onCommit } = setup(0);
    fireEvent.dblClick(screen.getByRole("button"));
    fireEvent.input(screen.getByRole("textbox"), { target: { value: "-6" } });
    fireEvent.keyDown(screen.getByRole("textbox"), { key: "Escape" });
    fireEvent.dblClick(screen.getByRole("button"));
    fireEvent.input(screen.getByRole("textbox"), { target: { value: "loud" } });
    fireEvent.keyDown(screen.getByRole("textbox"), { key: "Enter" });
    expect(onCommit).not.toHaveBeenCalled();
  });

  it("-∞ dB を数値とは別の表示にする", () => {
    setup(null);
    expect(screen.getByRole("button").textContent).toBe("-∞dB");
  });
});
