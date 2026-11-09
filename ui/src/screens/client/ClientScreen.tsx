// SPDX-License-Identifier: AGPL-3.0-only
// 子機の画面（Container）。
// 上半分は接続（招待の入力、承認待ち、接続中）、下半分は送信の設定
// （入力デバイス、モノラルかステレオか、送信ミュート、送信レベル、入力レベルメーター）。
// キーボードやオーディオインターフェースのライン入力はステレオで送れる。切り替えると親機では
// ミュートの新しいストリップになる（docs/adr/0035-client-input-expansion.md）。
// 送信の設定は接続前から操作でき、参加する前にマイクを確かめられるようにする。
// マイクを選んだだけでは送信を始めず、送信ミュートの解除は利用者の明示的な操作だけで行う。

import { useState } from "preact/hooks";
import { Fader } from "../../components/fader/Fader";
import { kSendLevelScale } from "../../components/fader/fader-scale";
import { Meter, type MeterSample } from "../../components/meter/Meter";
import { MuteButton } from "../../components/mute-button/MuteButton";
import {
  kNotificationHistoryWidthPx,
  NotificationHistory,
} from "../../components/toast/NotificationHistory";
import { PlannedButton } from "../../components/planned-button/PlannedButton";
import { t, type MessageKey } from "../../i18n/i18n";
import type { EngineClient } from "../../ipc/engine-client";
import type { ClientStore } from "../../state/client-store";
import type { MeterStore } from "../../state/meter-store";
import type { OptimisticStore } from "../../state/optimistic-store";
import type { ToastStore } from "../../state/toast-store";
import { useStore } from "../../state/use-store";
import { errorText, notifyPlanned } from "../shared/feedback";
import { ToastLayer } from "../shared/ToastLayer";
import { useCommand } from "../shared/use-command";
import { useContinuousGain } from "../shared/use-continuous-gain";
import { connectionView } from "./client-status";
import "./ClientScreen.css";

/** 子機の画面の props。依存はコンポジション（main.ts）から注入する。 */
export interface ClientScreenProps {
  client: EngineClient;
  store: ClientStore;
  optimistic: OptimisticStore;
  meters: MeterStore;
  toasts: ToastStore;
  /** 起動時に渡された招待（--join や vocroz:// から）。入力欄に入れておくだけで、自動では参加しない。 */
  initialInvite?: string;
}

/** 今後のバージョンで実装する子機の機能。 */
export const kClientPlannedFeatures: readonly MessageKey[] = [
  "feature.app_audio",
  "feature.return_monitor",
];

/** select の「入力なし」を表す値。デバイスの永続 ID と重ならない値にする。 */
const kNoInputValue = "";

/** 子機の画面。 */
export function ClientScreen({
  client,
  store,
  optimistic,
  meters,
  toasts,
  initialInvite = "",
}: ClientScreenProps) {
  const state = useStore(store);
  const toastState = useStore(toasts);
  const gain = useContinuousGain(client, optimistic);
  const command = useCommand(client, toasts);
  const [invite, setInvite] = useState(initialInvite);
  const [historyOpen, setHistoryOpen] = useState(false);

  if (state === null) {
    return <div class="screen-loading">{t("common.connecting_engine")}</div>;
  }

  const view = connectionView(state.client_state);
  const statusText =
    state.client_state === "FAILED" && state.failure_code !== null
      ? `${t("client.FAILED")} (${state.failure_code})`
      : t(`client.${state.client_state}`);
  const readMeter = (): MeterSample | undefined => {
    const reading = meters.read(state.entity_id);
    return reading === undefined
      ? undefined
      : { levelDb: reading.levelDb, ageMs: meters.now() - reading.receivedAt };
  };
  const busy = command.isPending(state.entity_id);

  const join = (event: Event) => {
    event.preventDefault();
    const trimmed = invite.trim();
    if (trimmed === "") return;
    command.send(state.entity_id, state.revision, { type: "join", invite: trimmed });
  };

  return (
    <div class="client-screen">
      <header class="client-header">
        <div class="client-header__title">{t("client.title")}</div>
        <div
          class={`client-header__state client-header__state--${state.client_state.toLowerCase()}`}
        >
          {statusText}
        </div>
        <button
          type="button"
          class="client-button"
          aria-pressed={historyOpen}
          onClick={() => setHistoryOpen((open) => !open)}
        >
          {t("status.history")}
        </button>
      </header>
      <div class="client-body">
        <main class="client-main">
          <section class="client-card" aria-label={t("client.connection")}>
            <h2>{t("client.connection")}</h2>
            {view === "invite" ? (
              <form class="client-invite" onSubmit={join}>
                {state.client_state === "FAILED" && state.failure_code !== null ? (
                  <p class="client-alert" role="alert">
                    {errorText(state.failure_code)}
                  </p>
                ) : null}
                {state.client_state === "DISCONNECTED" && state.failure_code !== null ? (
                  <p class="client-note">{errorText(state.failure_code)}</p>
                ) : null}
                <label class="client-field">
                  <span>{t("client.invite_label")}</span>
                  <input
                    value={invite}
                    placeholder={t("client.invite_placeholder")}
                    autoComplete="off"
                    spellcheck={false}
                    onInput={(event) => setInvite(event.currentTarget.value)}
                  />
                </label>
                <button
                  type="submit"
                  class="client-button client-button--primary"
                  disabled={busy || invite.trim() === ""}
                >
                  {t("client.join")}
                </button>
              </form>
            ) : null}
            {view === "waiting" ? (
              <div class="client-waiting">
                <p>
                  {state.client_state === "WAITING_APPROVAL"
                    ? t("client.waiting_approval_hint", { session: state.session_name ?? "" })
                    : t("client.connecting_hint")}
                </p>
                <button
                  type="button"
                  class="client-button"
                  disabled={busy}
                  onClick={() => command.send(state.entity_id, state.revision, { type: "leave" })}
                >
                  {t("common.cancel")}
                </button>
              </div>
            ) : null}
            {view === "connected" ? (
              <div class="client-connected">
                <p>{t("client.connected_to", { session: state.session_name ?? "" })}</p>
                <button
                  type="button"
                  class="client-button"
                  disabled={busy}
                  onClick={() => command.send(state.entity_id, state.revision, { type: "leave" })}
                >
                  {t("client.leave")}
                </button>
              </div>
            ) : null}
          </section>
          <section class="client-card" aria-label={t("client.send_settings")}>
            <h2>{t("client.send_settings")}</h2>
            <label class="client-field">
              <span>{t("client.input_device")}</span>
              <select
                value={state.selected_input_id ?? kNoInputValue}
                disabled={busy}
                onChange={(event) => {
                  const value = event.currentTarget.value;
                  command.send(state.entity_id, state.revision, {
                    type: "select_input",
                    device_id: value === kNoInputValue ? null : value,
                  });
                }}
              >
                <option value={kNoInputValue}>{t("client.no_input")}</option>
                {state.input_devices.map((device) => (
                  <option key={device.id} value={device.id}>
                    {device.name}
                  </option>
                ))}
              </select>
            </label>
            <label class="client-field">
              <span>{t("client.input_channels")}</span>
              <select
                value={String(state.input_channels)}
                disabled={busy}
                onChange={(event) =>
                  command.send(state.entity_id, state.revision, {
                    type: "set_input_channels",
                    channels: event.currentTarget.value === "2" ? 2 : 1,
                  })
                }
              >
                <option value="1">{t("client.mono")}</option>
                <option value="2">{t("client.stereo")}</option>
              </select>
            </label>
            {state.input_missing ? (
              <p class="client-alert" role="alert">
                {t("client.input_missing")}
              </p>
            ) : null}
            <div class="client-send">
              <div class="client-send__level">
                <Meter read={readMeter} label={t("client.input_meter")} />
                <Fader
                  value={gain.displayValue(state.entity_id, state.send_level_db)}
                  scale={kSendLevelScale}
                  label={t("client.send_level")}
                  onInput={(value) => gain.onInput(state.entity_id, value)}
                  onCommit={(value) => gain.onCommit(state.entity_id, value)}
                />
              </div>
              <div class="client-send__mute">
                <MuteButton
                  muted={state.send_muted}
                  text={state.send_muted ? t("client.send_muted") : t("client.sending")}
                  label={t("client.send_mute_label")}
                  pending={busy}
                  onToggle={() =>
                    command.send(state.entity_id, state.revision, {
                      type: "set_send_mute",
                      muted: !state.send_muted,
                    })
                  }
                />
                <p class="client-note">
                  {state.send_muted ? t("client.send_muted_hint") : t("client.sending_hint")}
                </p>
              </div>
            </div>
            <div class="client-planned">
              {kClientPlannedFeatures.map((feature) => (
                <PlannedButton
                  key={feature}
                  text={t(feature)}
                  title={t("feature.planned_hint")}
                  onClick={() => notifyPlanned(feature, toasts)}
                />
              ))}
            </div>
          </section>
        </main>
        {historyOpen ? (
          <NotificationHistory items={toastState.history} onClose={() => setHistoryOpen(false)} />
        ) : null}
      </div>
      <ToastLayer toasts={toasts} rightInset={historyOpen ? kNotificationHistoryWidthPx : 0} />
    </div>
  );
}
