// SPDX-License-Identifier: AGPL-3.0-only
// ルーティングの構成のうち、プラグインの挿入の編集欄。
// 挿入はほかの構成と同じくステージングして「適用」で反映する（docs/adr/0042-plugin-insert.md）。
// 読み込みの状態とエディターはエンジンが適用中の挿入にだけ持つので、適用前の挿入には出さない。

import { useState } from "preact/hooks";
import { t, type MessageKey } from "../../i18n/i18n";
import type {
  PluginEntry,
  PluginInsertFailure,
  PluginInsertState,
  PluginInsertStatus,
  RoutingCheck,
  RoutingConfig,
} from "../../ipc/messages";
import type { NodeOption } from "./RoutingEditor";
import {
  addInsert,
  kRoutingIdPrefix,
  reloadInsert,
  removeInsert,
  rowClassOf,
  updateInsertPlugin,
} from "./routing-edit";

/** 挿入の編集欄の props。 */
export interface InsertsSectionProps {
  config: RoutingConfig;
  /** 挿す先にできるノード（ストリップ、バス、Master）。 */
  targets: readonly NodeOption[];
  /** 挿せるプラグイン（VSTi を除いた一覧）。 */
  plugins: readonly PluginEntry[];
  /** 挿入の読み込みの状態。適用していない挿入は undefined。 */
  statusOf: (insertId: string) => PluginInsertStatus | undefined;
  /** ステージング中の構成の検証の結果。適用中の構成を出しているときは null。 */
  check: RoutingCheck | null;
  labelOf: (entityId: string) => string;
  newId: (prefix: string) => string;
  disabled: boolean;
  onChange: (config: RoutingConfig) => void;
  onOpenEditor: (insertId: string) => void;
}

const kStateKey: Record<PluginInsertState, MessageKey> = {
  LOADING: "plugin.state.LOADING",
  READY: "plugin.state.READY",
  FAILED: "plugin.state.FAILED",
};

const kFailureKey: Record<PluginInsertFailure, MessageKey> = {
  NOT_FOUND: "plugin.failure.NOT_FOUND",
  INSTANTIATE_FAILED: "plugin.failure.INSTANTIATE_FAILED",
  UNSUPPORTED_LAYOUT: "plugin.failure.UNSUPPORTED_LAYOUT",
  PROCESSING_FAILED: "plugin.failure.PROCESSING_FAILED",
};

/** 挿入の状態の文言。失敗したときは理由を出す。 */
function statusText(status: PluginInsertStatus | undefined): string {
  if (status === undefined) return t("plugin.state.STAGED");
  if (status.state === "FAILED" && status.failure !== null) return t(kFailureKey[status.failure]);
  return t(kStateKey[status.state]);
}

/** プラグインの表示名。一覧にないプラグインは識別子で示す。 */
function pluginLabel(plugins: readonly PluginEntry[], pluginId: string): string {
  return plugins.find((p) => p.plugin_id === pluginId)?.name ?? pluginId;
}

/** プラグインの挿入の編集欄。 */
export function InsertsSection({
  config,
  targets,
  plugins,
  statusOf,
  check,
  labelOf,
  newId,
  disabled,
  onChange,
  onOpenEditor,
}: InsertsSectionProps) {
  const [target, setTarget] = useState<string>("master");
  const first = plugins[0];
  // 選んでいた挿す先がなくなったら（バスを消したなど）、Master に戻す。
  const selectedTarget = targets.some((n) => n.entityId === target) ? target : "master";

  return (
    <section class="routing-section" aria-label={t("routing.inserts")}>
      <header class="routing-section__header">
        <h2>{t("routing.inserts")}</h2>
        <div class="routing-section__actions">
          <select
            value={selectedTarget}
            disabled={disabled}
            aria-label={t("routing.insert_target_label")}
            onChange={(e) => setTarget(e.currentTarget.value)}
          >
            {targets.map((n) => (
              <option key={n.entityId} value={n.entityId}>
                {n.label}
              </option>
            ))}
          </select>
          <button
            type="button"
            disabled={disabled || first === undefined}
            onClick={() => {
              if (first === undefined) return;
              onChange(
                addInsert(config, {
                  entity_id: newId(kRoutingIdPrefix.insert),
                  target: selectedTarget,
                  plugin_id: first.plugin_id,
                }),
              );
            }}
          >
            {t("routing.add_insert")}
          </button>
        </div>
      </header>
      <p class="routing-section__hint">
        {first === undefined ? t("routing.insert_no_plugins") : t("routing.insert_hint")}
      </p>
      {config.inserts.map((insert) => {
        const status = statusOf(insert.entity_id);
        const known = plugins.some((p) => p.plugin_id === insert.plugin_id);
        const name = pluginLabel(plugins, insert.plugin_id);
        return (
          <div
            key={insert.entity_id}
            class={rowClassOf(check, insert.entity_id)}
            data-entity-id={insert.entity_id}
          >
            <span class="routing-row__mark">
              {status?.state === "FAILED" || check?.issue_entity_id === insert.entity_id ? "!" : ""}
            </span>
            <span class="routing-row__name">{labelOf(insert.target)}</span>
            <select
              value={insert.plugin_id}
              disabled={disabled}
              aria-label={t("routing.insert_plugin_label", { name: labelOf(insert.target) })}
              onChange={(e) =>
                onChange(updateInsertPlugin(config, insert.entity_id, e.currentTarget.value))
              }
            >
              {known ? null : <option value={insert.plugin_id}>{insert.plugin_id}</option>}
              {plugins.map((p) => (
                <option key={p.plugin_id} value={p.plugin_id}>
                  {p.name}
                </option>
              ))}
            </select>
            <span
              class={`routing-row__state${status?.state === "FAILED" ? " routing-row__state--failed" : ""}`}
            >
              {statusText(status)}
            </span>
            <button
              type="button"
              disabled={status?.state !== "READY"}
              aria-label={t("routing.open_editor_label", { name })}
              onClick={() => onOpenEditor(insert.entity_id)}
            >
              {t("routing.open_editor")}
            </button>
            {status?.state === "FAILED" ? (
              <button
                type="button"
                disabled={disabled}
                aria-label={t("routing.reload_insert_label", { name })}
                onClick={() =>
                  onChange(reloadInsert(config, insert.entity_id, newId(kRoutingIdPrefix.insert)))
                }
              >
                {t("routing.reload_insert")}
              </button>
            ) : null}
            <button
              type="button"
              class="routing-row__remove"
              disabled={disabled}
              aria-label={t("routing.remove_label", { name })}
              onClick={() => onChange(removeInsert(config, insert.entity_id))}
            >
              {t("routing.remove")}
            </button>
          </div>
        );
      })}
    </section>
  );
}
