// SPDX-License-Identifier: AGPL-3.0-only
// 受信の統計値（実測のジッターとバッファの長さ）の最新値を保持するストア。
// 統計値はエンジンが 1 Hz ほどで送る観測値で、スナップショットには含まれない。
// メーターより十分に遅いので、Canvas ではなく通常の再描画で表示する。

import type { LinkStat, LinkStatsMessage } from "../ipc/messages";
import { Store } from "./store";

/** entity_id ごとの受信の統計値。 */
export type LinkStatsState = Readonly<Record<string, LinkStat>>;

/** 受信の統計値のストア。 */
export class LinkStatsStore extends Store<LinkStatsState> {
  constructor() {
    super({});
  }

  /**
   * エンジンから届いた統計値で置き換える。
   * 届いた一覧にないチャンネルは受信していないので、前の値を残さず消す（古い値を今の値に見せないため）。
   * @param message 統計値のメッセージ
   */
  apply(message: LinkStatsMessage): void {
    this.set(message.stats);
  }
}
