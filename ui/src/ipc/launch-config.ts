// SPDX-License-Identifier: AGPL-3.0-only
// 起動時にネイティブ側から受け取る設定（モード、表示言語、招待）を読む。
//
// JUCE 上では WebBrowserComponent の初期化データ（window.__JUCE__.initialisationData）で受け取る。
// 親機モードでは、ミキサーの窓とルーティングの窓を別の WebView で開くので、どちらの画面かと、
// コマンドの request_id の接頭辞も受け取る（docs/adr/0038-routing-window.md）。
// remote-pa は起動引数（--mode、--lang、--join）を検証してからここに渡す（docs/adr/0007-launch-interface.md）。
// ブラウザでの開発時は、同じ名前の URL のクエリ（?mode=client&lang=en&join=...）で指定する。

/** 画面のモード。起動ごとに固定し、起動中には切り替えない。 */
export type UiMode = "host" | "client";

/** 窓に表示する画面。 */
export type UiScreen = "main" | "routing";

/** 起動時の設定。 */
export interface LaunchConfig {
  mode: UiMode;
  /** 表示言語の指定。なければ OS の表示言語に合わせる。 */
  lang: string | undefined;
  /** 子機モードで最初に入力欄に入れておく招待。 */
  invite: string | undefined;
  /** 表示する画面。指定がなければモードの主画面。 */
  screen: UiScreen;
  /** コマンドの request_id の接頭辞。同じエンジンにつながる窓ごとに違う値をネイティブ側が渡す。 */
  requestPrefix: string | undefined;
}

/**
 * 初期化データの値を文字列として読む。JUCE は値を配列で渡すことがあるので、先頭の要素を使う。
 * @param value 初期化データの値
 */
function readString(value: unknown): string | undefined {
  const first = Array.isArray(value) ? (value as unknown[])[0] : value;
  return typeof first === "string" && first !== "" ? first : undefined;
}

/**
 * 起動時の設定を読む。初期化データを URL のクエリより優先する。
 * @param initialisationData JUCE の初期化データ。JUCE 上でなければ undefined
 * @param search URL のクエリ文字列（location.search）
 * @returns 起動時の設定。モードの指定が不正なら親機モード、画面の指定が不正なら主画面
 */
export function readLaunchConfig(
  initialisationData: Record<string, unknown> | undefined,
  search: string,
): LaunchConfig {
  const query = new URLSearchParams(search);
  const pick = (name: string) =>
    readString(initialisationData?.[name]) ?? readString(query.get(name));
  return {
    mode: pick("mode") === "client" ? "client" : "host",
    lang: pick("lang"),
    invite: pick("join"),
    screen: pick("screen") === "routing" ? "routing" : "main",
    requestPrefix: pick("request_prefix"),
  };
}
