// SPDX-License-Identifier: AGPL-3.0-only
// ルーティング画面（Container）。
// 構成の編集はすべてステージングとしてエンジンへ送り、検証の結果とともに戻った構成を表示する。
// 音が変わるのは「適用」を押したときだけで、遅延ノードを通るループは承認の操作がなければ適用しない
// （docs/adr/0026-routing-staging.md）。適用中のバスとセンドのフェーダーとミュートは下の段で操作する。

import { useState } from "preact/hooks";
import { t, type MessageKey } from "../../i18n/i18n";
import type { EngineClient } from "../../ipc/engine-client";
import type { ChannelStrip, RoutingConfig, RoutingInfo, RoutingIssue } from "../../ipc/messages";
import type { MixerStore } from "../../state/mixer-store";
import type { OptimisticStore } from "../../state/optimistic-store";
import type { ToastStore } from "../../state/toast-store";
import { useStore } from "../../state/use-store";
import { ToastLayer } from "../shared/ToastLayer";
import { useCommand } from "../shared/use-command";
import { useContinuousGain } from "../shared/use-continuous-gain";
import { RouteLevelsView } from "./RouteLevelsView";
import { RoutingEditor, type NodeOption } from "./RoutingEditor";
import { editingConfig, nextRoutingId } from "./routing-edit";
import "./RoutingScreen.css";

/** ルーティング画面の props。依存はコンポジション（main.ts）から注入する。 */
export interface RoutingScreenProps {
  client: EngineClient;
  store: MixerStore;
  optimistic: OptimisticStore;
  toasts: ToastStore;
  /**
   * 画面を閉じる。同じ窓で開いたときはミキサー画面に戻り、別の窓で開いたときは窓を閉じる。
   */
  onClose: () => void;
  /** 別の窓で開いているか。閉じるボタンの文言を変える。 */
  inOwnWindow?: boolean;
}

const kIssueKey: Record<RoutingIssue, MessageKey> = {
  INVALID_ID: "routing.issue.INVALID_ID",
  DUPLICATE_ID: "routing.issue.DUPLICATE_ID",
  TOO_MANY_ITEMS: "routing.issue.TOO_MANY_ITEMS",
  UNKNOWN_NODE: "routing.issue.UNKNOWN_NODE",
  INVALID_SEND: "routing.issue.INVALID_SEND",
  INVALID_SEND_TAP: "routing.issue.INVALID_SEND_TAP",
  DELAY_OUT_OF_RANGE: "routing.issue.DELAY_OUT_OF_RANGE",
  OUTPUT_CHANNEL_OUT_OF_RANGE: "routing.issue.OUTPUT_CHANNEL_OUT_OF_RANGE",
  OVERLAPPING_OUTPUTS: "routing.issue.OVERLAPPING_OUTPUTS",
  UNDELAYED_LOOP: "routing.issue.UNDELAYED_LOOP",
  UNKNOWN_PLUGIN: "routing.issue.UNKNOWN_PLUGIN",
  UNSUPPORTED_PLUGIN: "routing.issue.UNSUPPORTED_PLUGIN",
};

/** ノードの表示名を引く関数を作る。名前のないバスや出力は entity_id で示す。 */
function makeLabelOf(
  config: RoutingConfig,
  applied: RoutingConfig,
  channels: readonly ChannelStrip[],
): (entityId: string) => string {
  const labels = new Map<string, string>();
  labels.set("master", t("mixer.master"));
  for (const c of channels) labels.set(c.entity_id, c.name || c.short_id);
  // 適用中の構成のあとに編集中の構成を入れ、名前を変えたノードは編集中の名前で示す。
  for (const source of [applied, config]) {
    for (const b of source.buses) labels.set(b.entity_id, b.name || b.entity_id);
    for (const o of source.outputs) labels.set(o.entity_id, o.name || o.entity_id);
    for (const d of source.delays) {
      labels.set(d.entity_id, t("routing.delay_name", { ms: d.delay_ms }));
    }
  }
  const nodeLabel = (entityId: string) => labels.get(entityId) ?? entityId;
  for (const source of [applied, config]) {
    for (const s of source.sends) {
      labels.set(s.entity_id, `${nodeLabel(s.from)} → ${nodeLabel(s.to)}`);
    }
  }
  return nodeLabel;
}

/** 状態の行の文言。 */
function statusText(routing: RoutingInfo, labelOf: (id: string) => string): string {
  const staged = routing.staged;
  if (staged === null) return t("routing.state_applied");
  const { check } = staged;
  if (check.issue !== null) {
    const reason = t(kIssueKey[check.issue]);
    return check.issue_entity_id === null
      ? reason
      : t("routing.issue_at", { reason, name: labelOf(check.issue_entity_id) });
  }
  return check.needs_approval ? t("routing.needs_approval") : t("routing.ready");
}

/** ルーティング画面。 */
export function RoutingScreen({
  client,
  store,
  optimistic,
  toasts,
  onClose,
  inOwnWindow = false,
}: RoutingScreenProps) {
  const state = useStore(store);
  const command = useCommand(client, toasts);
  const gain = useContinuousGain(client, optimistic);
  // 承認は、承認したときの revision の構成にだけ効く。ステージングし直したら改めて承認が要る。
  const [approvedRevision, setApprovedRevision] = useState<number | null>(null);

  if (state === null) {
    return <div class="screen-loading">{t("common.connecting_engine")}</div>;
  }

  const { routing, channels } = state;
  const config = editingConfig(routing);
  const check = routing.staged?.check ?? null;
  const labelOf = makeLabelOf(config, routing.applied, channels);
  const pending = command.isPending(routing.entity_id);
  const approved = approvedRevision === routing.revision;

  const option = (entityId: string): NodeOption => ({ entityId, label: labelOf(entityId) });
  const strips = channels.map((c) => option(c.entity_id));
  const buses = config.buses.map((b) => option(b.entity_id));
  const delays = config.delays.map((d) => option(d.entity_id));
  const sources = [option("master"), ...strips, ...buses, ...delays];
  const destinations = [
    option("master"),
    ...buses,
    ...config.outputs.map((o) => option(o.entity_id)),
    ...delays,
  ];

  const stage = (next: RoutingConfig) =>
    command.send(routing.entity_id, routing.revision, { type: "stage_routing", config: next });
  const canApply =
    check !== null && check.issue === null && (!check.needs_approval || approved) && !pending;

  return (
    <div class="routing-screen">
      <header class="routing-header">
        <button type="button" class="routing-header__back" onClick={onClose}>
          {t(inOwnWindow ? "routing.close" : "routing.back")}
        </button>
        <h1 class="routing-header__title">{t("routing.title")}</h1>
        <p
          class={`routing-header__status${check?.issue != null ? " routing-header__status--issue" : ""}`}
          role="status"
        >
          {statusText(routing, labelOf)}
        </p>
        {check?.needs_approval === true && check.issue === null ? (
          <label class="routing-header__approve">
            <input
              type="checkbox"
              checked={approved}
              disabled={pending}
              onChange={(e) =>
                setApprovedRevision(e.currentTarget.checked ? routing.revision : null)
              }
            />
            {t("routing.approve_loop")}
          </label>
        ) : null}
        <button
          type="button"
          class="routing-header__apply"
          disabled={!canApply}
          onClick={() =>
            command.send(routing.entity_id, routing.revision, {
              type: "commit_routing",
              approve_feedback: check?.needs_approval === true && approved,
            })
          }
        >
          {t("routing.apply")}
        </button>
        <button
          type="button"
          disabled={routing.staged === null || pending}
          onClick={() =>
            command.send(routing.entity_id, routing.revision, { type: "discard_routing" })
          }
        >
          {t("routing.discard")}
        </button>
      </header>
      <div class="routing-body">
        <RoutingEditor
          config={config}
          check={check}
          sources={sources}
          destinations={destinations}
          strips={strips}
          labelOf={labelOf}
          newId={(prefix) => nextRoutingId(prefix, routing, config)}
          disabled={pending}
          onChange={stage}
        />
      </div>
      <RouteLevelsView
        levels={routing.levels}
        labelOf={labelOf}
        gainOf={(level) => gain.displayValue(level.entity_id, level.gain_db)}
        isPending={command.isPending}
        onGainInput={gain.onInput}
        onGainCommit={gain.onCommit}
        onToggleMute={(level) =>
          command.send(level.entity_id, level.revision, {
            type: "set_route_mute",
            muted: !level.muted,
          })
        }
      />
      <ToastLayer toasts={toasts} rightInset={0} />
    </div>
  );
}
