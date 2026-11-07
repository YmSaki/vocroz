// SPDX-License-Identifier: AGPL-3.0-only
import { describe, expect, it } from "vitest";
import { formatDb, parseDb, roundDb } from "./db";

describe("formatDb", () => {
  it("-∞ と 0 dB と符号付きの値を区別して表示する", () => {
    expect(formatDb(null)).toBe("-∞");
    expect(formatDb(0)).toBe("0.0");
    expect(formatDb(-0.04)).toBe("0.0");
    expect(formatDb(3.46)).toBe("+3.5");
    expect(formatDb(-12)).toBe("-12.0");
  });
});

describe("roundDb", () => {
  it("0.1 dB 刻みに丸め、-0 を返さない", () => {
    expect(roundDb(1.234)).toBe(1.2);
    expect(Object.is(roundDb(-0.01), 0)).toBe(true);
  });
});

describe("parseDb", () => {
  it("数値を範囲内に収めて返す", () => {
    expect(parseDb("-6.26", -60, 10, true)).toBe(-6.3);
    expect(parseDb("+20", -60, 10, true)).toBe(10);
    expect(parseDb("-100", -60, 10, true)).toBe(-60);
    expect(parseDb(" 3 dB ", -60, 10, true)).toBe(3);
  });

  it("全角や Unicode のマイナス記号も受け付ける", () => {
    expect(parseDb("－３", -60, 10, true)).toBe(-3);
    expect(parseDb("−5", -60, 10, true)).toBe(-5);
  });

  it("-inf は -∞ を許すときだけ -∞ にする", () => {
    expect(parseDb("-inf", -60, 10, true)).toBeNull();
    expect(parseDb("-∞", -60, 10, true)).toBeNull();
    expect(parseDb("-inf", -60, 12, false)).toBe(-60);
  });

  it("読み取れない入力は undefined を返す", () => {
    expect(parseDb("abc", -60, 10, true)).toBeUndefined();
    expect(parseDb("", -60, 10, true)).toBeUndefined();
    expect(parseDb("1e3", -60, 10, true)).toBeUndefined();
  });
});
