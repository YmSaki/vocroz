// SPDX-License-Identifier: AGPL-3.0-only
// 窓の操作の求めが、ネイティブ側の決めた文字列で送られることを確かめる。

import { describe, expect, it, vi } from "vitest";
import { JuceWindowControl, kWindowEventId } from "./juce-window";

describe("JuceWindowControl", () => {
  it("開くと閉じるを、窓の操作のイベントで求める", () => {
    const backend = {
      emitEvent: vi.fn(),
      addEventListener: vi.fn(),
      removeEventListener: vi.fn(),
    };
    const control = new JuceWindowControl(backend);
    control.openRouting();
    control.close();
    expect(backend.emitEvent.mock.calls).toEqual([
      [kWindowEventId, "open_routing"],
      [kWindowEventId, "close"],
    ]);
  });
});
