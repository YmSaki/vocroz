// SPDX-License-Identifier: AGPL-3.0-only
// 画面上部のステータス帯。
// セッション名、状態、接続人数、出力デバイス、警告件数、更新の通知を常に表示する。
// 出力デバイスの消失は解決するまでここに警告として残す。
// 出力デバイスはここで選ぶ。選択は ACK を待ってから表示に反映する（リストの値はエンジンの状態だけで決める）。
// 全チャンネル共通の目標遅延（ジッターバッファの上限）もここで選ぶ。

import { t } from "../../i18n/i18n";
import type { SessionInfo } from "../../ipc/messages";
import { errorText } from "../shared/feedback";

/**
 * 目標遅延の選択肢（ms）。エンジンが受け付ける 10〜250 ms のうち、合奏で使う短い側を細かく刻む。
 * 既定の 20 ms は低遅延の運用での上限（docs/adr/0002-opus-low-delay.md の低遅延プロファイル）。
 */
export const kLatencyTargetOptionsMs: readonly number[] = [10, 15, 20, 30, 40, 60, 80, 120, 250];

/** 選択肢に現在の値を加える。保存されていた値が選択肢にないときも、今の値をそのまま示すため。 */
function latencyOptions(current: number): number[] {
  return kLatencyTargetOptionsMs.includes(current)
    ? [...kLatencyTargetOptionsMs]
    : [...kLatencyTargetOptionsMs, current].sort((a, b) => a - b);
}

// リストの「出力なし」と「見つからないデバイス」の値。エンドポイント ID は空にならず、
// 中かっこで始まるので、どちらとも重ならない。
const kNoOutput = "";
const kLostOutput = "\u0000lost";

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
  /** 目標遅延を選ぶ。 */
  onSetLatencyTarget: (targetMs: number) => void;
  onCreateSession: () => void;
  /** 出力デバイスを選んだ。null は「出力なし」。 */
  onSelectOutput: (deviceId: string | null) => void;
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
  onSetLatencyTarget,
  onCreateSession,
  onSelectOutput,
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
  // LOST のときは「見つからないデバイス」を選択中として示す。挿し直したデバイスを選んでも
  // 値が変わり、選び直しとして送れるようにするため。
  const selectedOutput =
    session.output_device_state === "OPEN"
      ? (session.output_device_id ?? kNoOutput)
      : deviceLost
        ? kLostOutput
        : kNoOutput;
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
        <select
          class="status-bar__select"
          aria-label={t("output.select")}
          value={selectedOutput}
          disabled={sessionPending}
          onChange={(event) => {
            const value = (event.target as HTMLSelectElement).value;
            onSelectOutput(value === kNoOutput ? null : value);
          }}
        >
          <option value={kNoOutput}>{t("output.none_option")}</option>
          {session.output_devices.map((device) => (
            <option key={device.id} value={device.id}>
              {device.name}
            </option>
          ))}
          {deviceLost ? (
            <option value={kLostOutput} disabled>
              {t("output.lost_option")}
            </option>
          ) : null}
        </select>
        {deviceLost ? <span class="status-bar__value">{t("output.LOST")}</span> : null}
      </div>
      <div class={`status-bar__item${warningCount > 0 ? " status-bar__item--warning" : ""}`}>
        <span class="status-bar__label">{t("status.warnings")}</span>
        <span class="status-bar__value">{warningCount}</span>
      </div>
      <label class="status-bar__item" title={t("status.latency_hint")}>
        <span class="status-bar__label">{t("status.latency")}</span>
        <select
          class="status-bar__select"
          aria-label={t("status.latency_label")}
          value={String(session.latency_target_ms)}
          disabled={sessionPending}
          onChange={(e) => onSetLatencyTarget(Number(e.currentTarget.value))}
        >
          {latencyOptions(session.latency_target_ms).map((ms) => (
            <option key={ms} value={String(ms)}>
              {t("status.latency_option", { ms })}
            </option>
          ))}
        </select>
      </label>
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
