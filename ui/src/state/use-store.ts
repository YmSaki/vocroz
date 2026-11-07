// SPDX-License-Identifier: AGPL-3.0-only
// ストアの値を Preact のコンポーネントで購読するフック。

import { useEffect, useState } from "preact/hooks";
import type { Store } from "./store";

/**
 * ストアの現在の値を返し、値が変わったら再描画する。
 * @param store 購読するストア
 */
export function useStore<T>(store: Store<T>): T {
  const [value, setValue] = useState(() => store.get());
  useEffect(() => {
    // 購読を始めるまでに変わった分を取りこぼさないよう、購読の直後に読み直す。
    const unsubscribe = store.subscribe(() => setValue(store.get()));
    setValue(store.get());
    return unsubscribe;
  }, [store]);
  return value;
}
