// SPDX-License-Identifier: AGPL-3.0-only
import { beforeEach, describe, expect, it } from "vitest";
import { setLocale } from "../../i18n/i18n";
import { stripInputLabel } from "./strip-input";

describe("stripInputLabel", () => {
  beforeEach(() => setLocale("ja"));

  it("モノラルの 1 本目のマイクにはラベルを出さない", () => {
    expect(stripInputLabel({ source_kind: "MICROPHONE", channel_count: 1, source_number: 1 })).toBe(
      "",
    );
  });

  it("2 本目以降のマイクには番号を出す", () => {
    expect(stripInputLabel({ source_kind: "MICROPHONE", channel_count: 1, source_number: 2 })).toBe(
      "マイク 2",
    );
  });

  it("アプリ音声とステレオを並べて出す", () => {
    expect(stripInputLabel({ source_kind: "APP_AUDIO", channel_count: 2, source_number: 1 })).toBe(
      "アプリ音声 · ステレオ",
    );
    expect(stripInputLabel({ source_kind: "MICROPHONE", channel_count: 2, source_number: 1 })).toBe(
      "マイク · ステレオ",
    );
  });
});
