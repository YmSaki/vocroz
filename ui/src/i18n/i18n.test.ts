// SPDX-License-Identifier: AGPL-3.0-only
import { afterEach, describe, expect, it } from "vitest";
import en from "./en.json";
import ja from "./ja.json";
import { detectLocale, setLocale, t } from "./i18n";

afterEach(() => setLocale("ja"));

describe("文字列リソース", () => {
  it("日本語と英語のキーがそろっている", () => {
    expect(Object.keys(en).sort()).toEqual(Object.keys(ja).sort());
  });

  it("埋め込みの名前が日本語と英語で一致する", () => {
    const placeholders = (text: string) => [...text.matchAll(/\{(\w+)\}/g)].map((m) => m[1]).sort();
    for (const key of Object.keys(ja) as (keyof typeof ja)[]) {
      expect(placeholders(en[key]), key).toEqual(placeholders(ja[key]));
    }
  });
});

describe("detectLocale", () => {
  it("起動引数の指定を OS の言語より優先する", () => {
    expect(detectLocale("en", ["ja-JP"])).toBe("en");
  });

  it("指定がなければ OS の表示言語に合わせる", () => {
    expect(detectLocale(undefined, ["en-US", "ja"])).toBe("en");
    expect(detectLocale(undefined, ["ja-JP"])).toBe("ja");
  });

  it("対応していない言語は日本語にする", () => {
    expect(detectLocale(undefined, ["fr-FR", "de"])).toBe("ja");
    expect(detectLocale("xx", [])).toBe("ja");
  });
});

describe("t", () => {
  it("現在の言語の文言に値を埋め込む", () => {
    setLocale("en");
    expect(t("status.update_available", { version: "1.2.3" })).toBe("Version 1.2.3 is available");
    setLocale("ja");
    expect(t("status.update_available", { version: "1.2.3" })).toBe("更新版 1.2.3 が利用可能です");
  });
});
