// SPDX-License-Identifier: AGPL-3.0-only
// ルーティングの構成の編集欄。バス、追加の出力、遅延ノード、センド、入力の手動ディレイの 5 つの一覧を出す。
// 編集のたびに新しい構成を onChange で返すだけで、構成は持たない（表示するのはエンジンから届いた構成）。
// 文字と数値の欄は、入力の途中ではなく確定（change）したときにだけ返し、エンジンへの送信を打鍵ごとにしない。

import type { JSX } from "preact";
import { t } from "../../i18n/i18n";
import type { BusKind, RoutingCheck, RoutingConfig, SendTap } from "../../ipc/messages";
import {
  addBus,
  addDelay,
  addInputDelay,
  addOutput,
  addSend,
  kMaxOutputFirstChannel,
  kRoutingIdPrefix,
  removeInputDelay,
  removeNode,
  removeSend,
  updateBus,
  updateDelay,
  updateInputDelay,
  updateOutput,
  updateSend,
} from "./routing-edit";

/** センドの端点の選択肢。 */
export interface NodeOption {
  entityId: string;
  label: string;
}

/** 編集欄の props。 */
export interface RoutingEditorProps {
  config: RoutingConfig;
  /** ステージング中の構成の検証の結果。適用中の構成を出しているときは null。 */
  check: RoutingCheck | null;
  /** 送り元にできるノード（Master、ストリップ、バス、遅延ノード）。 */
  sources: readonly NodeOption[];
  /** 送り先にできるノード（Master、バス、出力、遅延ノード）。 */
  destinations: readonly NodeOption[];
  /** 手動ディレイを付けられるストリップ。 */
  strips: readonly NodeOption[];
  /** ノードの表示名。 */
  labelOf: (entityId: string) => string;
  /** 新しい entity_id を作る。 */
  newId: (prefix: string) => string;
  /** 構成を送っている間は true。 */
  disabled: boolean;
  onChange: (config: RoutingConfig) => void;
}

const kBusKinds: readonly BusKind[] = ["USER", "CUE"];
const kTaps: readonly SendTap[] = ["POST_FADER", "PRE_FADER"];
const kBusKindKey = { USER: "routing.bus_kind.USER", CUE: "routing.bus_kind.CUE" } as const;
const kTapKey = {
  POST_FADER: "routing.tap.POST_FADER",
  PRE_FADER: "routing.tap.PRE_FADER",
} as const;

/** 出力の先頭チャンネルの選択肢（0 始まりの偶数）。0〜1 はメインの出力なので含めない。 */
const kOutputChannels: readonly number[] = Array.from(
  { length: kMaxOutputFirstChannel / 2 },
  (_, i) => (i + 1) * 2,
);

function inputValue(event: JSX.TargetedEvent<HTMLInputElement | HTMLSelectElement>): string {
  return event.currentTarget.value;
}

/** ルーティングの構成の編集欄。 */
export function RoutingEditor({
  config,
  check,
  sources,
  destinations,
  strips,
  labelOf,
  newId,
  disabled,
  onChange,
}: RoutingEditorProps) {
  // 問題のある項目とループに含まれる項目は、色だけでなく印の文字でも示す（色覚に頼らないため）。
  const rowClass = (entityId: string) => {
    if (check?.issue_entity_id === entityId) return "routing-row routing-row--issue";
    if (check?.looped_entity_ids.includes(entityId) === true)
      return "routing-row routing-row--loop";
    return "routing-row";
  };
  const marker = (entityId: string) => {
    if (check?.issue_entity_id === entityId) return <span class="routing-row__mark">!</span>;
    if (check?.looped_entity_ids.includes(entityId) === true) {
      return <span class="routing-row__mark">↻</span>;
    }
    return <span class="routing-row__mark" />;
  };
  const removeButton = (name: string, onClick: () => void) => (
    <button
      type="button"
      class="routing-row__remove"
      disabled={disabled}
      aria-label={t("routing.remove_label", { name })}
      onClick={onClick}
    >
      {t("routing.remove")}
    </button>
  );
  // 新しいセンドの端点は、まだ結線していない組のうち最初のものにする。同じ組を重ねると、適用できない
  // 構成をいきなり作ってしまうため。送り元はバス、Master、遅延ノードの順に探す（ストリップからの送りは
  // 作ったあとで選び直す）。
  const defaultSend = () => {
    const fromCandidates = [
      ...config.buses.map((b) => b.entity_id),
      "master",
      ...config.delays.map((d) => d.entity_id),
    ];
    for (const from of fromCandidates) {
      for (const to of destinations) {
        if (to.entityId === from || to.entityId === "master") continue;
        if (config.sends.some((s) => s.from === from && s.to === to.entityId)) continue;
        return { from, to: to.entityId };
      }
    }
    return null;
  };
  const send = defaultSend();
  const freeStrip = strips.find((s) => !config.input_delays.some((d) => d.strip_id === s.entityId));

  return (
    <div class="routing-editor">
      <section class="routing-section" aria-label={t("routing.buses")}>
        <header class="routing-section__header">
          <h2>{t("routing.buses")}</h2>
          <button
            type="button"
            disabled={disabled}
            onClick={() => {
              const n = config.buses.length + 1;
              onChange(
                addBus(config, {
                  entity_id: newId(kRoutingIdPrefix.bus),
                  name: t("routing.default_bus_name", { n }),
                  kind: "USER",
                }),
              );
            }}
          >
            {t("routing.add_bus")}
          </button>
        </header>
        {config.buses.length === 0 ? (
          <p class="routing-section__empty">{t("routing.empty")}</p>
        ) : null}
        {config.buses.map((bus) => (
          <div key={bus.entity_id} class={rowClass(bus.entity_id)} data-entity-id={bus.entity_id}>
            {marker(bus.entity_id)}
            <input
              type="text"
              value={bus.name}
              disabled={disabled}
              aria-label={t("routing.name_label", { name: labelOf(bus.entity_id) })}
              onChange={(e) => onChange(updateBus(config, bus.entity_id, { name: inputValue(e) }))}
            />
            <select
              value={bus.kind}
              disabled={disabled}
              aria-label={t("routing.bus_kind_label", { name: labelOf(bus.entity_id) })}
              onChange={(e) =>
                onChange(updateBus(config, bus.entity_id, { kind: inputValue(e) as BusKind }))
              }
            >
              {kBusKinds.map((kind) => (
                <option key={kind} value={kind}>
                  {t(kBusKindKey[kind])}
                </option>
              ))}
            </select>
            {removeButton(labelOf(bus.entity_id), () =>
              onChange(removeNode(config, bus.entity_id)),
            )}
          </div>
        ))}
      </section>

      <section class="routing-section" aria-label={t("routing.outputs")}>
        <header class="routing-section__header">
          <h2>{t("routing.outputs")}</h2>
          <button
            type="button"
            disabled={disabled}
            onClick={() => {
              const n = config.outputs.length + 1;
              onChange(
                addOutput(config, {
                  entity_id: newId(kRoutingIdPrefix.output),
                  name: t("routing.default_output_name", { n }),
                }),
              );
            }}
          >
            {t("routing.add_output")}
          </button>
        </header>
        <p class="routing-section__hint">{t("routing.main_output_hint")}</p>
        {config.outputs.map((output) => (
          <div
            key={output.entity_id}
            class={rowClass(output.entity_id)}
            data-entity-id={output.entity_id}
          >
            {marker(output.entity_id)}
            <input
              type="text"
              value={output.name}
              disabled={disabled}
              aria-label={t("routing.name_label", { name: labelOf(output.entity_id) })}
              onChange={(e) =>
                onChange(updateOutput(config, output.entity_id, { name: inputValue(e) }))
              }
            />
            <select
              value={String(output.device_channel)}
              disabled={disabled}
              aria-label={t("routing.output_channel_label", { name: labelOf(output.entity_id) })}
              onChange={(e) =>
                onChange(
                  updateOutput(config, output.entity_id, { device_channel: Number(inputValue(e)) }),
                )
              }
            >
              {kOutputChannels.map((first) => (
                <option key={first} value={String(first)}>
                  {t("routing.output_channels", { first: first + 1, second: first + 2 })}
                </option>
              ))}
            </select>
            {removeButton(labelOf(output.entity_id), () =>
              onChange(removeNode(config, output.entity_id)),
            )}
          </div>
        ))}
      </section>

      <section class="routing-section" aria-label={t("routing.delays")}>
        <header class="routing-section__header">
          <h2>{t("routing.delays")}</h2>
          <button
            type="button"
            disabled={disabled}
            onClick={() => onChange(addDelay(config, newId(kRoutingIdPrefix.delay)))}
          >
            {t("routing.add_delay")}
          </button>
        </header>
        <p class="routing-section__hint">{t("routing.delay_hint")}</p>
        {config.delays.map((delay) => (
          <div
            key={delay.entity_id}
            class={rowClass(delay.entity_id)}
            data-entity-id={delay.entity_id}
          >
            {marker(delay.entity_id)}
            <span class="routing-row__name">{labelOf(delay.entity_id)}</span>
            <input
              type="number"
              min={3}
              max={2000}
              step={1}
              value={delay.delay_ms}
              disabled={disabled}
              aria-label={t("routing.delay_ms_label", { name: labelOf(delay.entity_id) })}
              onChange={(e) => {
                const ms = Number(inputValue(e));
                if (Number.isFinite(ms)) onChange(updateDelay(config, delay.entity_id, ms));
              }}
            />
            <span class="routing-row__unit">ms</span>
            {removeButton(labelOf(delay.entity_id), () =>
              onChange(removeNode(config, delay.entity_id)),
            )}
          </div>
        ))}
      </section>

      <section class="routing-section" aria-label={t("routing.sends")}>
        <header class="routing-section__header">
          <h2>{t("routing.sends")}</h2>
          <button
            type="button"
            disabled={disabled || send === null}
            onClick={() => {
              if (send === null) return;
              onChange(
                addSend(config, {
                  entity_id: newId(kRoutingIdPrefix.send),
                  from: send.from,
                  to: send.to,
                  tap: "POST_FADER",
                }),
              );
            }}
          >
            {t("routing.add_send")}
          </button>
        </header>
        <p class="routing-section__hint">{t("routing.send_hint")}</p>
        {config.sends.map((s) => (
          <div key={s.entity_id} class={rowClass(s.entity_id)} data-entity-id={s.entity_id}>
            {marker(s.entity_id)}
            <NodeSelect
              value={s.from}
              options={sources}
              labelOf={labelOf}
              disabled={disabled}
              label={t("routing.send_from_label", { name: s.entity_id })}
              onChange={(from) => onChange(updateSend(config, s.entity_id, { from }))}
            />
            <span class="routing-row__arrow" aria-hidden="true">
              →
            </span>
            <NodeSelect
              value={s.to}
              options={destinations}
              labelOf={labelOf}
              disabled={disabled}
              label={t("routing.send_to_label", { name: s.entity_id })}
              onChange={(to) => onChange(updateSend(config, s.entity_id, { to }))}
            />
            <select
              value={s.tap}
              disabled={disabled}
              aria-label={t("routing.tap_label", { name: s.entity_id })}
              onChange={(e) =>
                onChange(updateSend(config, s.entity_id, { tap: inputValue(e) as SendTap }))
              }
            >
              {kTaps.map((tap) => (
                <option key={tap} value={tap}>
                  {t(kTapKey[tap])}
                </option>
              ))}
            </select>
            {removeButton(s.entity_id, () => onChange(removeSend(config, s.entity_id)))}
          </div>
        ))}
      </section>

      <section class="routing-section" aria-label={t("routing.input_delays")}>
        <header class="routing-section__header">
          <h2>{t("routing.input_delays")}</h2>
          <button
            type="button"
            disabled={disabled || freeStrip === undefined}
            onClick={() => {
              if (freeStrip !== undefined) onChange(addInputDelay(config, freeStrip.entityId));
            }}
          >
            {t("routing.add_input_delay")}
          </button>
        </header>
        <p class="routing-section__hint">{t("routing.input_delay_hint")}</p>
        {config.input_delays.map((delay) => (
          <div
            key={delay.strip_id}
            class={rowClass(delay.strip_id)}
            data-entity-id={delay.strip_id}
          >
            {marker(delay.strip_id)}
            <span class="routing-row__name">{labelOf(delay.strip_id)}</span>
            <input
              type="number"
              min={0}
              max={1000}
              step={1}
              value={delay.delay_ms}
              disabled={disabled}
              aria-label={t("routing.delay_ms_label", { name: labelOf(delay.strip_id) })}
              onChange={(e) => {
                const ms = Number(inputValue(e));
                if (Number.isFinite(ms)) onChange(updateInputDelay(config, delay.strip_id, ms));
              }}
            />
            <span class="routing-row__unit">ms</span>
            {removeButton(labelOf(delay.strip_id), () =>
              onChange(removeInputDelay(config, delay.strip_id)),
            )}
          </div>
        ))}
      </section>
    </div>
  );
}

interface NodeSelectProps {
  value: string;
  options: readonly NodeOption[];
  labelOf: (entityId: string) => string;
  disabled: boolean;
  label: string;
  onChange: (entityId: string) => void;
}

/** センドの端点の選択。消えたノードを指しているときも、その値を選択肢に残して見えるようにする。 */
function NodeSelect({ value, options, labelOf, disabled, label, onChange }: NodeSelectProps) {
  const known = options.some((o) => o.entityId === value);
  return (
    <select
      value={value}
      disabled={disabled}
      aria-label={label}
      onChange={(e) => onChange(inputValue(e))}
    >
      {known ? null : <option value={value}>{labelOf(value)}</option>}
      {options.map((o) => (
        <option key={o.entityId} value={o.entityId}>
          {o.label}
        </option>
      ))}
    </select>
  );
}
