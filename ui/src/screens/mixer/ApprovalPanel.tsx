// SPDX-License-Identifier: AGPL-3.0-only
// 参加の承認待ちを表示する非モーダルのサイドパネル。
// ミキシングを止めずに、承認と拒否ができるようにする。

import { t } from "../../i18n/i18n";
import type { PendingParticipant } from "../../ipc/messages";

/** 承認パネルの props。 */
export interface ApprovalPanelProps {
  pending: readonly PendingParticipant[];
  isPending: (entityId: string) => boolean;
  onApprove: (participant: PendingParticipant) => void;
  onReject: (participant: PendingParticipant) => void;
  onClose: () => void;
}

/** パネルの幅（px）。トーストをパネルに被せないよう、画面側でもこの値を使う。 */
export const kApprovalPanelWidthPx = 320;

/** 承認パネル。 */
export function ApprovalPanel({
  pending,
  isPending,
  onApprove,
  onReject,
  onClose,
}: ApprovalPanelProps) {
  return (
    <aside
      class="approval-panel"
      style={{ width: `${kApprovalPanelWidthPx}px` }}
      aria-label={t("approval.title")}
    >
      <header class="approval-panel__header">
        <h2>{t("approval.title")}</h2>
        <button type="button" class="approval-panel__close" onClick={onClose}>
          {t("common.close")}
        </button>
      </header>
      {pending.length === 0 ? (
        <p class="approval-panel__empty">{t("approval.empty")}</p>
      ) : (
        <ul class="approval-panel__list">
          {pending.map((participant) => (
            <li key={participant.entity_id} class="approval-panel__item">
              <div class="approval-panel__who">
                <span class="approval-panel__name">{participant.name}</span>
                <span class="approval-panel__id">{participant.short_id}</span>
              </div>
              <div class="approval-panel__actions">
                <button
                  type="button"
                  class="approval-panel__approve"
                  disabled={isPending(participant.entity_id)}
                  onClick={() => onApprove(participant)}
                >
                  {t("approval.approve")}
                </button>
                <button
                  type="button"
                  class="approval-panel__reject"
                  disabled={isPending(participant.entity_id)}
                  onClick={() => onReject(participant)}
                >
                  {t("approval.reject")}
                </button>
              </div>
            </li>
          ))}
        </ul>
      )}
    </aside>
  );
}
