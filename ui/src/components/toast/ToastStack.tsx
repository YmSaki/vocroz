// SPDX-License-Identifier: AGPL-3.0-only
// 非モーダル通知（トースト）の表示（Presentational Component）。
// 画面の右下に積むが、右端の Master のフェーダーとミュートに被らないよう、
// 呼び出し側が rightInset で Master の幅だけ左に寄せる。

import { t } from "../../i18n/i18n";
import "./Toast.css";

/** トースト 1 件の表示内容。 */
export interface ToastItem {
  id: number;
  severity: "info" | "warning" | "error";
  text: string;
}

/** トーストの表示の props。 */
export interface ToastStackProps {
  /** 表示する通知。古い順。 */
  toasts: readonly ToastItem[];
  /** 右端から空ける幅（px）。 */
  rightInset: number;
  /** 閉じるボタンが押されたとき。 */
  onDismiss: (id: number) => void;
}

/** トーストの表示。 */
export function ToastStack({ toasts, rightInset, onDismiss }: ToastStackProps) {
  return (
    <div class="toast-stack" style={{ right: `${rightInset + 12}px` }} aria-live="polite">
      {toasts.map((toast) => (
        <div
          key={toast.id}
          class={`toast toast--${toast.severity}`}
          role={toast.severity === "info" ? "status" : "alert"}
        >
          <span class="toast__text">{toast.text}</span>
          <button
            type="button"
            class="toast__close"
            aria-label={t("toast.dismiss")}
            onClick={() => onDismiss(toast.id)}
          >
            ×
          </button>
        </div>
      ))}
    </div>
  );
}
