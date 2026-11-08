// SPDX-License-Identifier: AGPL-3.0-only
import { describe, expect, it } from "vitest";
import { displayLevel, kMeterFloorDb, kMeterStaleMs } from "./Meter";

describe("displayLevel", () => {
  it("新しい値はそのまま表示する", () => {
    expect(displayLevel({ levelDb: -12, ageMs: 0 })).toBe(-12);
    expect(displayLevel({ levelDb: -12, ageMs: kMeterStaleMs })).toBe(-12);
  });

  it("500 ms を超えて新しい値が届かなければ、バーを止めたままにせず減衰させる", () => {
    const later = displayLevel({ levelDb: -12, ageMs: kMeterStaleMs + 100 });
    expect(later).not.toBeNull();
    expect(later as number).toBeLessThan(-12);
    expect(displayLevel({ levelDb: -12, ageMs: kMeterStaleMs + 1000 })).toBeNull();
  });

  it("無音と値なしは描かない", () => {
    expect(displayLevel(undefined)).toBeNull();
    expect(displayLevel({ levelDb: null, ageMs: 0 })).toBeNull();
    expect(kMeterFloorDb).toBeLessThan(0);
  });
});
