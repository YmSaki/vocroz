// SPDX-License-Identifier: AGPL-3.0-only
import { describe, expect, it } from "vitest";
import { readLaunchConfig } from "./launch-config";

describe("readLaunchConfig", () => {
  it("指定がなければ親機モードにする", () => {
    expect(readLaunchConfig(undefined, "")).toEqual({
      mode: "host",
      lang: undefined,
      invite: undefined,
    });
  });

  it("JUCE の初期化データを URL のクエリより優先し、配列の値も読む", () => {
    const config = readLaunchConfig({ mode: ["client"], join: "7KQ2-M9XD" }, "?mode=host&lang=en");
    expect(config).toEqual({ mode: "client", lang: "en", invite: "7KQ2-M9XD" });
  });

  it("不正なモードは親機モードにする", () => {
    expect(readLaunchConfig(undefined, "?mode=other").mode).toBe("host");
  });
});
