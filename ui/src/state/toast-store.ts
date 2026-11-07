// SPDX-License-Identifier: AGPL-3.0-only
// 非モーダル通知（トースト）と通知履歴を保持するストア。
//
// 同時に表示するのは最大 3 件で、あふれた分は古いものから表示を外し、通知履歴にだけ残す。
// 表示時間は種類で決める。情報（保存完了、招待のコピー、開発中）は 4 秒、警告（切断、パケット欠損など）は 8 秒。

import { kBrowserTimers, type Timers } from "../utils/timers";
import { Store } from "./store";

/** 通知の種類。 */
export type ToastSeverity = "info" | "warning" | "error";

/** 通知 1 件。text は表示する文言（ローカライズ済み）。 */
export interface Toast {
  id: number;
  severity: ToastSeverity;
  text: string;
  /** 通知した時刻（Date.now() の値）。履歴の表示に使う。 */
  createdAt: number;
}

/** トーストの状態。 */
export interface ToastState {
  /** 表示中の通知。古い順。 */
  visible: readonly Toast[];
  /** 通知履歴。新しい順。 */
  history: readonly Toast[];
}

/** 同時に表示する通知の最大数。画面を通知で埋めず、操作の邪魔をしないため。 */
export const kMaxVisibleToasts = 3;
/** 通知履歴に残す最大数。本番の数時間でメモリを増やし続けないため。 */
export const kMaxToastHistory = 100;
/** 情報の通知を表示する時間（ms）。 */
export const kInfoToastMs = 4000;
/** 警告とエラーの通知を表示する時間（ms）。 */
export const kWarningToastMs = 8000;

/** トーストのストア。 */
export class ToastStore extends Store<ToastState> {
  private nextId = 1;
  private readonly timersById = new Map<number, unknown>();

  /** @param timers 自動で消すためのタイマー */
  constructor(
    private readonly timers: Timers = kBrowserTimers,
    private readonly clock: () => number = Date.now,
  ) {
    super({ visible: [], history: [] });
  }

  /**
   * 通知を出す。
   * @param severity 通知の種類
   * @param text 表示する文言（ローカライズ済み）
   * @returns 通知の ID
   */
  push(severity: ToastSeverity, text: string): number {
    const toast: Toast = { id: this.nextId++, severity, text, createdAt: this.clock() };
    const current = this.get();
    let visible = [...current.visible, toast];
    while (visible.length > kMaxVisibleToasts) {
      const [oldest, ...rest] = visible;
      if (oldest !== undefined) this.cancelTimer(oldest.id);
      visible = rest;
    }
    const history = [toast, ...current.history].slice(0, kMaxToastHistory);
    this.set({ visible, history });
    const duration = severity === "info" ? kInfoToastMs : kWarningToastMs;
    this.timersById.set(
      toast.id,
      this.timers.setTimeout(() => this.dismiss(toast.id), duration),
    );
    return toast.id;
  }

  /**
   * 通知の表示を外す。履歴には残る。
   * @param id 通知の ID
   */
  dismiss(id: number): void {
    this.cancelTimer(id);
    const current = this.get();
    if (!current.visible.some((t) => t.id === id)) return;
    this.set({ ...current, visible: current.visible.filter((t) => t.id !== id) });
  }

  private cancelTimer(id: number): void {
    const handle = this.timersById.get(id);
    if (handle === undefined) return;
    this.timers.clearTimeout(handle);
    this.timersById.delete(id);
  }
}
