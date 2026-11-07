// SPDX-License-Identifier: AGPL-3.0-only
// 値を 1 つ保持し、変更を購読者に配信する最小限のストア。
// 画面ごとのストア（mixer-store.ts など）がこれを使って状態を保持する。

/** 状態の変更を受け取るリスナー。 */
export type StoreListener = () => void;

/** 値を 1 つ保持し、変更を配信するストア。 */
export class Store<T> {
  private value: T;
  private readonly listeners = new Set<StoreListener>();

  /** @param initial 初期値 */
  constructor(initial: T) {
    this.value = initial;
  }

  /** 現在の値を返す。 */
  get(): T {
    return this.value;
  }

  /**
   * 値を置き換えて購読者に知らせる。同じ参照なら何もしない。
   * @param next 新しい値
   */
  set(next: T): void {
    if (Object.is(next, this.value)) return;
    this.value = next;
    for (const listener of this.listeners) listener();
  }

  /**
   * 変更を購読する。
   * @returns 購読を解除する関数
   */
  subscribe(listener: StoreListener): () => void {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }
}
