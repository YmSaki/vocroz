// SPDX-License-Identifier: AGPL-3.0-only
// テスト用の Timers のフェイク。advance で時刻を進めたときだけタイマーが発火し、テストが決定的になる。

import type { Timers } from "./timers";

interface Scheduled {
  id: number;
  at: number;
  callback: () => void;
}

/** 手動で時刻を進める Timers。 */
export class FakeTimers implements Timers {
  private current = 0;
  private nextId = 1;
  private scheduled: Scheduled[] = [];

  now(): number {
    return this.current;
  }

  setTimeout(callback: () => void, ms: number): unknown {
    const id = this.nextId++;
    this.scheduled.push({ id, at: this.current + Math.max(0, ms), callback });
    return id;
  }

  clearTimeout(handle: unknown): void {
    this.scheduled = this.scheduled.filter((s) => s.id !== handle);
  }

  /**
   * 時刻を進め、その間に期限が来たタイマーを期限の順に発火する。
   * @param ms 進める時間（ms）
   */
  advance(ms: number): void {
    const end = this.current + ms;
    for (;;) {
      const due = this.scheduled
        .filter((s) => s.at <= end)
        .sort((a, b) => a.at - b.at || a.id - b.id)[0];
      if (due === undefined) break;
      this.scheduled = this.scheduled.filter((s) => s.id !== due.id);
      this.current = due.at;
      due.callback();
    }
    this.current = end;
  }

  /** 予約中のタイマーの数。 */
  pendingCount(): number {
    return this.scheduled.length;
  }
}
