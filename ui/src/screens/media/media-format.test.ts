// SPDX-License-Identifier: AGPL-3.0-only
// メディア画面の表示用の整形と、録る対象の切り替え。
import { describe, expect, it } from "vitest";
import { formatClock, toggleSource } from "./media-format";

describe("formatClock", () => {
  it("1 時間未満は m:ss で、秒未満は切り捨てる", () => {
    expect(formatClock(0)).toBe("0:00");
    expect(formatClock(61_999)).toBe("1:01");
    expect(formatClock(59 * 60_000 + 59_000)).toBe("59:59");
  });

  it("1 時間以上は h:mm:ss で示す", () => {
    expect(formatClock(3_600_000)).toBe("1:00:00");
    expect(formatClock(2 * 3_600_000 + 5 * 60_000 + 7_000)).toBe("2:05:07");
  });

  it("負の値は 0 として扱う", () => {
    expect(formatClock(-5)).toBe("0:00");
  });
});

describe("toggleSource", () => {
  it("新しい対象は末尾に足し、既にある対象の順を変えない", () => {
    expect(toggleSource(["master", "ch1"], "bus-1", true)).toEqual(["master", "ch1", "bus-1"]);
  });

  it("外した対象だけを除く", () => {
    expect(toggleSource(["master", "ch1", "bus-1"], "ch1", false)).toEqual(["master", "bus-1"]);
  });

  it("同じ対象を二重に足さない", () => {
    expect(toggleSource(["master"], "master", true)).toEqual(["master"]);
  });
});
