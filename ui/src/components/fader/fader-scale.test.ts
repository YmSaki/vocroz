// SPDX-License-Identifier: AGPL-3.0-only
import { describe, expect, it } from "vitest";
import { kMixerFaderScale, kSendLevelScale } from "./fader-scale";

describe("kMixerFaderScale", () => {
  it("一番下は -∞ dB、一番上は +10 dB にする", () => {
    expect(kMixerFaderScale.fromPosition(0)).toBeNull();
    expect(kMixerFaderScale.toPosition(null)).toBe(0);
    expect(kMixerFaderScale.fromPosition(1)).toBe(10);
  });

  it("位置と dB 値を往復しても値が変わらない", () => {
    for (const db of [-60, -30, -12, -6, 0, 3.5, 10]) {
      expect(kMixerFaderScale.fromPosition(kMixerFaderScale.toPosition(db))).toBeCloseTo(db, 1);
    }
  });

  it("0 dB を 3/4 の高さに置く", () => {
    expect(kMixerFaderScale.toPosition(0)).toBeCloseTo(0.75);
  });
});

describe("kSendLevelScale", () => {
  it("-60〜+12 dB で、-∞ にならない", () => {
    expect(kSendLevelScale.fromPosition(0)).toBe(-60);
    expect(kSendLevelScale.fromPosition(1)).toBe(12);
    expect(kSendLevelScale.allowsNegInf).toBe(false);
  });
});
