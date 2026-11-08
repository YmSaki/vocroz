// SPDX-License-Identifier: AGPL-3.0-only
// 画面上部のステータス帯。
// セッション名、状態、接続人数、出力デバイス、警告件数、更新の通知を常に表示する。
// 出力デバイスの消失は解決するまでここに警告として残す。

import { t } from "../../i18n/i18n";
import type { SessionInfo } from "../../ipc/messages";
import { errorText } from "../shared/feedback";

/** ステータス帯の props。 */
export interface StatusBarProps {
  session: SessionInfo;
  /** 接続中（CONNECTED と RECONNECTING）の人数。 */
  connectedCount: number;
  /** 警告の件数。 */
  warningCount: number;
  /** 承認待ちの人数。 */
  pendingCount: number;
  /** セッションの操作の ACK を待っているか。 */
  sessionPending: boolean;
  approvalOpen: boolean;
  historyOpen: boolean;
  onCreateSession: () => void;
  onEndSession: () => void;
  onCopyInvite: () => void;
  onToggleApproval: () => void;
  onToggleHistory: () => void;
  onUpdate: () => void;
}

/** ステータス帯。 */
export function StatusBar({
  session,
  connectedCount,
  warningCount,
  pendingCount,
  sessionPending,
  approvalOpen,
  historyOpen,
  onCreateSession,
  onEndSession,
  onCopyInvite,
  onToggleApproval,
  onToggleHistory,
  onUpdate,
}: StatusBarProps) {
  const stateText =
    session.state === "ERROR" && session.error_code !== null
      ? `${t("session.ERROR")} (${session.error_code})`
      : t(`session.${session.state}`);
  const deviceLost = session.output_device_state === "LOST";
  const deviceText =
    session.output_device_state === "OPEN"
      ? (session.output_device_name ?? "")
      : t(`output.${session.output_device_state}`);
  const canEnd = session.state === "READY" || session.state === "ACTIVE";
  return (
    <header class="status-bar">
      <div class="status-bar__item status-bar__session">
        <span class="status-bar__label">{t("status.session")}</span>
        <span class="status-bar__value">{session.name || t("status.no_session")}</span>
      </div>
      <div
        class={`status-bar__item status-bar__state status-bar__state--${session.state.toLowerCase()}`}
        title={session.error_code !== null ? errorText(session.error_code) : undefined}
      >
        {stateText}
      </div>
      <div class="status-bar__item">
        <span class="status-bar__label">{t("status.connected")}</span>
        <span class="status-bar__value">{t("status.people", { count: connectedCount })}</span>
      </div>
      <div
        class={`status-bar__item${deviceLost ? " status-bar__item--alert" : ""}`}
        role={deviceLost ? "alert" : undefined}
      >
        <span class="status-bar__label">{t("status.output")}</span>
        <span class="status-bar__value">{deviceText}</span>
      </div>
      <div class={`status-bar__item${warningCount > 0 ? " status-bar__item--warning" : ""}`}>
        <span class="status-bar__label">{t("status.warnings")}</span>
        <span class="status-bar__value">{warningCount}</span>
      </div>
      <div class="status-bar__spacer" />
      {session.update_available !== null ? (
        <button
          type="button"
          class="status-bar__button status-bar__update"
          title={t("status.update_available", { version: session.update_available })}
          onClick={onUpdate}
        >
          {t("status.update")}
        </button>
      ) : null}
      <button
        type="button"
        class={`status-bar__button${pendingCount > 0 ? " status-bar__button--badge" : ""}`}
        aria-pressed={approvalOpen}
        onClick={onToggleApproval}
      >
        {t("status.approval")}
        {pendingCount > 0 ? <span class="status-bar__badge">{pendingCount}</span> : null}
      </button>
      <button
        type="button"
        class="status-bar__button"
        aria-pressed={historyOpen}
        onClick={onToggleHistory}
      >
        {t("status.history")}
      </button>
      {session.invite_url !== null ? (
        <button type="button" class="status-bar__button" onClick={onCopyInvite}>
          {t("status.copy_invite")}
        </button>
      ) : null}
      {session.state === "IDLE" ? (
        <button
          type="button"
          class="status-bar__button status-bar__primary"
          disabled={sessionPending}
          onClick={onCreateSession}
        >
          {t("status.create_session")}
        </button>
      ) : null}
      {canEnd ? (
        <button
          type="button"
          class="status-bar__button status-bar__danger"
          disabled={sessionPending}
          onClick={onEndSession}
        >
          {t("status.end_session")}
        </button>
      ) : null}
    </header>
  );
}
