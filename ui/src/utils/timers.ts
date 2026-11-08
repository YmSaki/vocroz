// SPDX-License-Identifier: AGPL-3.0-only
// 時刻とタイマーの抽象。テストで時刻を決定的に進められるよう、利用側に注入する（AGENTS.md 第 13 節）。

/** 時刻とタイマー。 */
export interface Timers {
  /** 単調増加する現在時刻（ms）。 */
  now(): number;
  /** callback を ms 後に 1 回呼ぶ。戻り値は clearTimeout に渡すハンドル。 */
  setTimeout(callback: () => void, ms: number): unknown;
  /** setTimeout で予約した呼び出しを取り消す。 */
  clearTimeout(handle: unknown): void;
}

/** ブラウザのタイマーを使う既定の実装。 */
export const kBrowserTimers: Timers = {
  now: () => performance.now(),
  setTimeout: (callback, ms) => globalThis.setTimeout(callback, ms),
  clearTimeout: (handle) => globalThis.clearTimeout(handle as ReturnType<typeof setTimeout>),
};
