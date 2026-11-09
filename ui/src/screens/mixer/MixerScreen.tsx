// SPDX-License-Identifier: AGPL-3.0-only
// 親機のミキサー画面（Container）。
// ストアを購読して描画し、操作をコマンドにしてエンジンへ送る。
//
// レイアウト: 上部のステータス帯、ルーティング画面と開発中の機能のツールバー、中央のチャンネル領域（横スクロール）、
// 必要に応じて承認パネルと通知履歴、右端に固定した Master。

import { useState } from "preact/hooks";
import { ConfirmDialog } from "../../components/confirm-dialog/ConfirmDialog";
import type { MeterSample } from "../../components/meter/Meter";
import {
  kNotificationHistoryWidthPx,
  NotificationHistory,
} from "../../components/toast/NotificationHistory";
import { PlannedButton } from "../../components/planned-button/PlannedButton";
import { t, type MessageKey } from "../../i18n/i18n";
import type { EngineClient } from "../../ipc/engine-client";
import type { MeterStore } from "../../state/meter-store";
import type { MixerStore } from "../../state/mixer-store";
import type { OptimisticStore } from "../../state/optimistic-store";
import type { ToastStore } from "../../state/toast-store";
import { useStore } from "../../state/use-store";
import { notifyPlanned } from "../shared/feedback";
import { ToastLayer } from "../shared/ToastLayer";
import { useCommand } from "../shared/use-command";
import { useContinuousGain } from "../shared/use-continuous-gain";
import { ApprovalPanel, kApprovalPanelWidthPx } from "./ApprovalPanel";
import { ChannelStripView } from "./ChannelStripView";
import { MasterStripView, kMasterStripWidthPx } from "./MasterStripView";
import { StatusBar } from "./StatusBar";
import { isStripWarning } from "./strip-status";
import "./MixerScreen.css";

/** ミキサー画面の props。依存はコンポジション（main.ts）から注入する。 */
export interface MixerScreenProps {
  client: EngineClient;
  store: MixerStore;
  optimistic: OptimisticStore;
  meters: MeterStore;
  toasts: ToastStore;
  /** ルーティング画面を開く。 */
  onOpenRouting: () => void;
  /** クリップボードへの書き込み。テストで差し替えられるようにする。 */
  writeClipboard?: (text: string) => Promise<void>;
}

/** 今後のバージョンで実装する機能。ボタンは置くが、押しても「開発中」と知らせるだけにする。 */
export const kMixerPlannedFeatures: readonly MessageKey[] = [
  "feature.media",
  "feature.virtual_device",
  "feature.vst",
  "feature.midi",
  "feature.video",
];

const defaultWriteClipboard = (text: string) => navigator.clipboard.writeText(text);

/** 親機のミキサー画面。 */
export function MixerScreen({
  client,
  store,
  optimistic,
  meters,
  toasts,
  onOpenRouting,
  writeClipboard = defaultWriteClipboard,
}: MixerScreenProps) {
  const state = useStore(store);
  const toastState = useStore(toasts);
  const gain = useContinuousGain(client, optimistic);
  const command = useCommand(client, toasts);
  const [approvalOpen, setApprovalOpen] = useState(false);
  const [historyOpen, setHistoryOpen] = useState(false);
  const [confirmEnd, setConfirmEnd] = useState(false);
  // 選択は表示上の強調だけで、エンジンの状態ではない。
  const [selected, setSelected] = useState<string | null>(null);

  if (state === null) {
    return <div class="screen-loading">{t("common.connecting_engine")}</div>;
  }

  const { session, channels, master, pending } = state;
  const readMeter = (entityId: string) => (): MeterSample | undefined => {
    const reading = meters.read(entityId);
    return reading === undefined
      ? undefined
      : { levelDb: reading.levelDb, ageMs: meters.now() - reading.receivedAt };
  };
  const connectedCount = channels.filter(
    (c) => c.connection_state === "CONNECTED" || c.connection_state === "RECONNECTING",
  ).length;
  const warningCount =
    channels.filter(isStripWarning).length + (session.output_device_state === "LOST" ? 1 : 0);

  const copyInvite = () => {
    if (session.invite_url === null) return;
    writeClipboard(session.invite_url).then(
      () => toasts.push("info", t("toast.invite_copied")),
      () => toasts.push("error", t("toast.invite_copy_failed")),
    );
  };

  return (
    <div class="mixer-screen">
      <StatusBar
        session={session}
        connectedCount={connectedCount}
        warningCount={warningCount}
        pendingCount={pending.length}
        sessionPending={command.isPending(session.entity_id)}
        approvalOpen={approvalOpen}
        historyOpen={historyOpen}
        onCreateSession={() =>
          command.send(session.entity_id, session.revision, { type: "create_session" })
        }
        onSelectOutput={(deviceId) =>
          command.send(session.entity_id, session.revision, {
            type: "select_output",
            device_id: deviceId,
          })
        }
        onEndSession={() => setConfirmEnd(true)}
        onCopyInvite={copyInvite}
        onToggleApproval={() => setApprovalOpen((open) => !open)}
        onToggleHistory={() => setHistoryOpen((open) => !open)}
        onUpdate={() =>
          toasts.push(
            "info",
            t("toast.update_after_session", { version: session.update_available ?? "" }),
          )
        }
      />
      <nav class="mixer-toolbar" aria-label={t("mixer.toolbar")}>
        <button type="button" class="mixer-toolbar__button" onClick={onOpenRouting}>
          {t("feature.routing")}
        </button>
        {kMixerPlannedFeatures.map((feature) => (
          <PlannedButton
            key={feature}
            text={t(feature)}
            title={t("feature.planned_hint")}
            onClick={() => notifyPlanned(feature, toasts)}
          />
        ))}
      </nav>
      <div class="mixer-main">
        <div class="mixer-channels" aria-label={t("mixer.channels")}>
          {channels.length === 0 ? (
            <p class="mixer-channels__empty">{t("mixer.no_channels")}</p>
          ) : (
            channels.map((channel) => (
              <ChannelStripView
                key={channel.entity_id}
                channel={channel}
                gainDb={gain.displayValue(channel.entity_id, channel.gain_db)}
                mutePending={command.isPending(channel.entity_id)}
                selected={selected === channel.entity_id}
                readMeter={readMeter(channel.entity_id)}
                onGainInput={(value) => gain.onInput(channel.entity_id, value)}
                onGainCommit={(value) => gain.onCommit(channel.entity_id, value)}
                onToggleMute={() =>
                  command.send(channel.entity_id, channel.revision, {
                    type: "set_channel_mute",
                    muted: !channel.channel_muted,
                  })
                }
                onSelect={() =>
                  setSelected((current) =>
                    current === channel.entity_id ? null : channel.entity_id,
                  )
                }
                onRemove={() =>
                  command.send(channel.entity_id, channel.revision, { type: "remove_strip" })
                }
                onPlanned={() => notifyPlanned("feature.insert_fx", toasts)}
              />
            ))
          )}
        </div>
        {approvalOpen ? (
          <ApprovalPanel
            pending={pending}
            isPending={command.isPending}
            onApprove={(p) =>
              command.send(p.entity_id, p.revision, { type: "approve_participant" })
            }
            onReject={(p) => command.send(p.entity_id, p.revision, { type: "reject_participant" })}
            onClose={() => setApprovalOpen(false)}
          />
        ) : null}
        {historyOpen ? (
          <NotificationHistory items={toastState.history} onClose={() => setHistoryOpen(false)} />
        ) : null}
        <MasterStripView
          master={master}
          gainDb={gain.displayValue(master.entity_id, master.gain_db)}
          mutePending={command.isPending(master.entity_id)}
          readMeter={readMeter(master.entity_id)}
          onGainInput={(value) => gain.onInput(master.entity_id, value)}
          onGainCommit={(value) => gain.onCommit(master.entity_id, value)}
          onToggleMute={() =>
            command.send(master.entity_id, master.revision, {
              type: "set_master_mute",
              muted: !master.master_muted,
            })
          }
        />
      </div>
      <ToastLayer
        toasts={toasts}
        rightInset={
          kMasterStripWidthPx +
          (approvalOpen ? kApprovalPanelWidthPx : 0) +
          (historyOpen ? kNotificationHistoryWidthPx : 0)
        }
      />
      {confirmEnd ? (
        <ConfirmDialog
          title={t("confirm.end_session_title")}
          message={t("confirm.end_session_message")}
          confirmText={t("confirm.end_session_confirm")}
          cancelText={t("common.cancel")}
          onCancel={() => setConfirmEnd(false)}
          onConfirm={() => {
            setConfirmEnd(false);
            command.send(session.entity_id, session.revision, { type: "end_session" });
          }}
        />
      ) : null}
    </div>
  );
}
