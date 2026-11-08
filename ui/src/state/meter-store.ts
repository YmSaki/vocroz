// SPDX-License-Identifier: AGPL-3.0-only
// メーターの最新値を保持するストア。
// メーターは 10〜30 Hz で届くので、画面全体を再描画せず、
// Canvas のメーターが描画のたびにここから値を読む（AGENTS.md 第 12 節「高頻度の描画は Canvas」）。

import type { MetersMessage } from "../ipc/messages";
import { kBrowserTimers, type Timers } from "../utils/timers";

/** メーター 1 本の最新値。 */
export interface MeterReading {
  /** ピーク（dBFS）。null は無音。 */
  levelDb: number | null;
  /** 値を受け取った時刻（ms）。鮮度の判定に使う。 */
  receivedAt: number;
}

/** entity ごとのメーターの最新値。 */
export class MeterStore {
  private readonly readings = new Map<string, MeterReading>();

  /** @param timers 受信時刻の取得に使うタイマー */
  constructor(private readonly timers: Timers = kBrowserTimers) {}

  /**
   * エンジンから届いたメーターの値を取り込む。
   * @param message メーターのメッセージ
   */
  apply(message: MetersMessage): void {
    const now = this.timers.now();
    for (const [entityId, levelDb] of Object.entries(message.levels)) {
      this.readings.set(entityId, { levelDb, receivedAt: now });
    }
  }

  /**
   * entity の最新値を返す。まだ届いていなければ undefined。
   * @param entityId メーターの entity
   */
  read(entityId: string): MeterReading | undefined {
    return this.readings.get(entityId);
  }

  /** 現在時刻（ms）。メーターの鮮度の判定に使う。 */
  now(): number {
    return this.timers.now();
  }
}
