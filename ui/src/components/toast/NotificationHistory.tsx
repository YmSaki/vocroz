// SPDX-License-Identifier: AGPL-3.0-only
// 通知履歴のパネル（Presentational Component）。
// トーストからあふれた通知や消えた通知を後から確認するための、非モーダルのパネル。

import { t } from "../../i18n/i18n";
import type { ToastItem } from "./ToastStack";
import "./Toast.css";

/** 通知履歴の 1 件。 */
export interface HistoryItem extends ToastItem {
  /** 通知した時刻（Date.now() の値）。 */
  createdAt: number;
}

/** 通知履歴のパネルの props。 */
export interface NotificationHistoryProps {
  /** 通知履歴。新しい順。 */
  items: readonly HistoryItem[];
  /** 閉じるボタンが押されたとき。 */
  onClose: () => void;
}

/** パネルの幅（px）。トーストをパネルに被せないよう、画面側でもこの値を使う。 */
export const kNotificationHistoryWidthPx = 320;

/** 時刻を HH:MM:SS で表示する。本番中の出来事を秒単位で追えるようにするため。 */
function formatTime(epochMs: number): string {
  return new Date(epochMs).toLocaleTimeString(undefined, { hour12: false });
}

/** 通知履歴のパネル。 */
export function NotificationHistory({ items, onClose }: NotificationHistoryProps) {
  return (
    <aside
      class="notification-history"
      style={{ width: `${kNotificationHistoryWidthPx}px` }}
      aria-label={t("history.title")}
    >
      <header class="notification-history__header">
        <h2>{t("history.title")}</h2>
        <button type="button" class="notification-history__close" onClick={onClose}>
          {t("common.close")}
        </button>
      </header>
      {items.length === 0 ? (
        <p class="notification-history__empty">{t("history.empty")}</p>
      ) : (
        <ol class="notification-history__list">
          {items.map((item) => (
            <li key={item.id} class={`notification-history__item toast--${item.severity}`}>
              <time>{formatTime(item.createdAt)}</time>
              <span>{item.text}</span>
            </li>
          ))}
        </ol>
      )}
    </aside>
  );
}
