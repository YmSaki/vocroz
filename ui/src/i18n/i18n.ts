// SPDX-License-Identifier: AGPL-3.0-only
// 文字列リソースのローダー。
// UI の文字列はすべてここを通し、キーで参照する（AGENTS.md 第 12 節）。
// 日本語は必須で、英語にないキーは日本語で表示する。

import en from "./en.json";
import ja from "./ja.json";

/** 対応するロケール。 */
export type Locale = "ja" | "en";

/** 文字列リソースのキー。日本語のリソースを正とする。 */
export type MessageKey = keyof typeof ja;

const kResources: Record<Locale, Partial<Record<MessageKey, string>>> = { ja, en };

let currentLocale: Locale = "ja";

/**
 * 表示言語を決める。起動引数（--lang）の指定を優先し、なければ OS の表示言語に合わせる。
 * WebView2 の navigator.languages は OS の表示言語を反映する。
 * @param override 起動引数で指定された言語。指定がなければ undefined
 * @param languages 優先順の言語タグ（navigator.languages）
 * @returns 表示言語。対応していない言語なら日本語
 */
export function detectLocale(override: string | undefined, languages: readonly string[]): Locale {
  for (const tag of override !== undefined ? [override, ...languages] : languages) {
    const primary = tag.toLowerCase().split("-")[0];
    if (primary === "ja" || primary === "en") return primary;
  }
  return "ja";
}

/**
 * 表示言語を切り替える。起動時に 1 回だけ呼ぶ。
 * @param locale 表示言語
 */
export function setLocale(locale: Locale): void {
  currentLocale = locale;
  document.documentElement.lang = locale;
}

/** 現在の表示言語を返す。 */
export function getLocale(): Locale {
  return currentLocale;
}

/**
 * キーに対応する文言を返す。文言中の {name} は params の値で置き換える。
 * @param key 文字列リソースのキー
 * @param params 埋め込む値
 * @returns 文言。現在の言語にキーがなければ日本語の文言
 */
export function t(key: MessageKey, params: Readonly<Record<string, string | number>> = {}): string {
  const template = kResources[currentLocale][key] ?? ja[key];
  return template.replace(/\{(\w+)\}/g, (whole, name: string) =>
    name in params ? String(params[name]) : whole,
  );
}

/**
 * 動的に組み立てたキー（状態の正規名から作ったキーなど）がリソースにあるかを確かめる。
 * @param key 確かめるキー
 */
export function isMessageKey(key: string): key is MessageKey {
  return key in ja;
}
