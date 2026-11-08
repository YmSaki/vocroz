// SPDX-License-Identifier: AGPL-3.0-only
// トーストのストアを購読して表示する Container。親機と子機の両方の画面で使う。

import { ToastStack } from "../../components/toast/ToastStack";
import type { ToastStore } from "../../state/toast-store";
import { useStore } from "../../state/use-store";

/** トーストの表示の props。 */
export interface ToastLayerProps {
  toasts: ToastStore;
  /** 右端から空ける幅（px）。親機では Master の幅を渡し、Master に被らないようにする。 */
  rightInset: number;
}

/** トーストのストアを購読して表示する。 */
export function ToastLayer({ toasts, rightInset }: ToastLayerProps) {
  const state = useStore(toasts);
  return (
    <ToastStack
      toasts={state.visible}
      rightInset={rightInset}
      onDismiss={(id) => toasts.dismiss(id)}
    />
  );
}
