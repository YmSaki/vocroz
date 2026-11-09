// SPDX-License-Identifier: AGPL-3.0-only
import { describe, expect, it } from "vitest";
import { isValidSrtpKey } from "../../ipc/fake-engine/fake-external-output";
import { generateSrtpKey } from "./srtp-key";

describe("generateSrtpKey", () => {
  it("30 バイトを Base64 にした inline: 形式の鍵を作る", () => {
    const key = generateSrtpKey((bytes) => bytes.forEach((_, i) => (bytes[i] = i + 1)));
    // control-protocol の JSON の例と同じ鍵（1 から 30 までのバイト）。
    expect(key).toBe("inline:AQIDBAUGBwgJCgsMDQ4PEBESExQVFhcYGRobHB0e");
    expect(isValidSrtpKey(key)).toBe(true);
  });

  it("既定では毎回違う鍵を作る", () => {
    expect(generateSrtpKey()).not.toBe(generateSrtpKey());
  });
});
