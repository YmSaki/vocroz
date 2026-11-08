// SPDX-License-Identifier: AGPL-3.0-only
// 連続値の楽観的更新の値を保持するストア。
//
// 表示はエンジンから届いた値で更新するのが原則だが、フェーダーのドラッグ中だけは
// 手元の操作に追従させるため、エンジンの値の上に UI の値を重ねて表示する。
// 重ねた値は、操作が終わって ACK がそろったとき、または ACK が期限内に来なかったときに外す。

import type { GainDb } from "../ipc/messages";
import { Store } from "./store";

/** entity 1 つ分の楽観的な値。 */
export interface OptimisticEntry {
  value: GainDb;
  /** 操作中（ドラッグ中など）か。操作中は ACK がそろっても値を外さない。 */
  active: boolean;
  /** 最後の操作以降に ACK がそろったか。操作の終了時にそろっていれば、すぐに値を外す。 */
  settled: boolean;
}

/** entity ごとの楽観的な値。 */
export type OptimisticValues = ReadonlyMap<string, OptimisticEntry>;

/** 連続値の楽観的更新を保持するストア。 */
export class OptimisticStore extends Store<OptimisticValues> {
  constructor() {
    super(new Map());
  }

  /**
   * 操作中の値を重ねる。
   * @param entityId 操作中の entity
   * @param value 操作者が指定した値
   */
  begin(entityId: string, value: GainDb): void {
    this.update(entityId, { value, active: true, settled: false });
  }

  /**
   * 操作が終わったことを記録する。値は ACK がそろうまで重ねたままにする。
   * @param entityId 操作が終わった entity
   */
  release(entityId: string): void {
    const entry = this.get().get(entityId);
    if (entry === undefined) return;
    if (entry.settled) this.update(entityId, undefined);
    else this.update(entityId, { ...entry, active: false });
  }

  /**
   * ACK がそろったときに呼ぶ。操作が終わっていれば値を外し、エンジンの値の表示に戻す。
   * @param entityId ACK がそろった entity
   */
  settle(entityId: string): void {
    const entry = this.get().get(entityId);
    if (entry === undefined) return;
    if (entry.active) this.update(entityId, { ...entry, settled: true });
    else this.update(entityId, undefined);
  }

  /**
   * ACK が期限内に来なかったときに呼ぶ。操作中でも値を外し、エンジンの値に戻す。
   * @param entityId 期限切れになった entity
   */
  revert(entityId: string): void {
    if (this.get().has(entityId)) this.update(entityId, undefined);
  }

  private update(entityId: string, entry: OptimisticEntry | undefined): void {
    const next = new Map(this.get());
    if (entry === undefined) next.delete(entityId);
    else next.set(entityId, entry);
    this.set(next);
  }
}
