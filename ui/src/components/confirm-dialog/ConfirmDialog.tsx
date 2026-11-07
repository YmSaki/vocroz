// SPDX-License-Identifier: AGPL-3.0-only
// 取り返しのつかない操作の確認ダイアログ（Presentational Component）。
// モーダルを使ってよいのは不可逆操作の確認だけ（AGENTS.md 第 12 節）。
// 誤って Enter を押しても実行されないよう、既定のフォーカスは「キャンセル」に置く。

import { useEffect, useRef } from "preact/hooks";
import "./ConfirmDialog.css";

/** 確認ダイアログの props。 */
export interface ConfirmDialogProps {
  title: string;
  message: string;
  confirmText: string;
  cancelText: string;
  onConfirm: () => void;
  onCancel: () => void;
}

/** 取り返しのつかない操作の確認ダイアログ。 */
export function ConfirmDialog({
  title,
  message,
  confirmText,
  cancelText,
  onConfirm,
  onCancel,
}: ConfirmDialogProps) {
  const cancelRef = useRef<HTMLButtonElement>(null);
  useEffect(() => cancelRef.current?.focus(), []);
  return (
    <div class="confirm-dialog__backdrop">
      <div
        class="confirm-dialog"
        role="alertdialog"
        aria-modal="true"
        aria-labelledby="confirm-dialog-title"
        aria-describedby="confirm-dialog-message"
        onKeyDown={(event) => {
          if (event.key === "Escape") onCancel();
        }}
      >
        <h2 id="confirm-dialog-title">{title}</h2>
        <p id="confirm-dialog-message">{message}</p>
        <div class="confirm-dialog__actions">
          <button type="button" ref={cancelRef} onClick={onCancel}>
            {cancelText}
          </button>
          <button type="button" class="confirm-dialog__danger" onClick={onConfirm}>
            {confirmText}
          </button>
        </div>
      </div>
    </div>
  );
}
