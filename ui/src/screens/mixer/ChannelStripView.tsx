// SPDX-License-Identifier: AGPL-3.0-only
// 入力チャンネルストリップ 1 本。
// 参加者名、短い識別 ID、入力の種類（2 本目以降の入力とステレオのとき）、状態欄、ミュートの表示（子機側と親機側を区別）、メーター、フェーダー、
// MUTE と SEL と返送の切り替えを常に表示する（ホバーしないと見えない操作は置かない）。
// 返送（Minus-Me）は子機 1 台に 1 本なので、同じ子機のどのストリップから切り替えても同じ返送が
// 変わる。初期値は OFF で、オペレーターが押したときだけ送る（docs/adr/0036-return-monitor.md）。
// 受信の実測値（ジッターとバッファの長さ）も、目標遅延を選ぶ判断材料として常に表示する。

import { Fader } from "../../components/fader/Fader";
import { kMixerFaderScale } from "../../components/fader/fader-scale";
import { Meter, type MeterSample } from "../../components/meter/Meter";
import { MuteBadge } from "../../components/mute-badge/MuteBadge";
import { MuteButton } from "../../components/mute-button/MuteButton";
import { PlannedButton } from "../../components/planned-button/PlannedButton";
import { t } from "../../i18n/i18n";
import type { ChannelStrip, GainDb, LinkStat } from "../../ipc/messages";
import { errorText } from "../shared/feedback";
import { stripInputLabel } from "./strip-input";
import { stripStatus } from "./strip-status";

/** チャンネルストリップの props。 */
export interface ChannelStripViewProps {
  channel: ChannelStrip;
  /** 表示するゲイン（操作中は手元の値）。 */
  gainDb: GainDb;
  /** ミュートのコマンドの ACK を待っているか。 */
  mutePending: boolean;
  /** 選択中か。 */
  selected: boolean;
  /** 受信の統計値。まだ届いていなければ undefined。 */
  link: LinkStat | undefined;
  readMeter: () => MeterSample | undefined;
  onGainInput: (value: GainDb) => void;
  onGainCommit: (value: GainDb) => void;
  onToggleMute: () => void;
  /** 返送の切り替え。ミュートと同じく ACK を待つ間は押せない。 */
  onToggleReturn: () => void;
  onSelect: () => void;
  onRemove: () => void;
  onPlanned: () => void;
}

/**
 * 統計値の表示。値がなければ、受信していないことを示す記号にする。
 * ストリップの幅に収めるため、10 ms 以上は小数を省く（その桁の差は判断に影響しない）。
 */
function linkValue(ms: number | null | undefined): string {
  return ms === null || ms === undefined
    ? t("mixer.link_none")
    : t("mixer.link_value", { ms: ms.toFixed(ms < 10 ? 1 : 0) });
}

/** 入力チャンネルストリップ。 */
export function ChannelStripView({
  channel,
  gainDb,
  mutePending,
  selected,
  link,
  readMeter,
  onGainInput,
  onGainCommit,
  onToggleMute,
  onToggleReturn,
  onSelect,
  onRemove,
  onPlanned,
}: ChannelStripViewProps) {
  const status = stripStatus(channel);
  // 原因コードはストリップの幅に収まらないので、ツールチップで説明する。
  const statusText = status.key === null ? "" : t(status.key);
  const inputLabel = stripInputLabel(channel);
  return (
    <section
      class={`strip${selected ? " strip--selected" : ""}`}
      // 同じ子機の入力は名前が同じなので、読み上げでも入力の種類で見分けられるようにする。
      aria-label={t("mixer.strip_label", {
        name: inputLabel === "" ? channel.name : `${channel.name} ${inputLabel}`,
      })}
      data-entity-id={channel.entity_id}
    >
      <header class="strip__header">
        <div class="strip__name" title={channel.name}>
          {channel.name}
        </div>
        <div class="strip__id">{channel.short_id}</div>
        {inputLabel !== "" && (
          <div class="strip__input" title={inputLabel}>
            {inputLabel}
          </div>
        )}
      </header>
      <div
        class={`strip__status${status.warning ? " strip__status--warning" : ""}`}
        title={
          channel.connection_state === "FAILED" && channel.error_code !== null
            ? errorText(channel.error_code)
            : undefined
        }
      >
        {statusText}
      </div>
      <dl class="strip__link" title={t("mixer.link_hint")}>
        <dt>{t("mixer.jitter")}</dt>
        <dd>{linkValue(link?.jitter_ms)}</dd>
        <dt>{t("mixer.buffer")}</dt>
        <dd>{linkValue(link?.buffer_ms)}</dd>
      </dl>
      <MuteBadge
        active={channel.send_muted}
        text={t("mute.send_muted")}
        title={t("mute.send_muted_hint")}
      />
      <PlannedButton
        text={t("feature.insert_fx")}
        title={t("feature.planned_hint")}
        onClick={onPlanned}
      />
      <div class="strip__body">
        <Meter read={readMeter} label={t("mixer.meter_label", { name: channel.name })} />
        <Fader
          value={gainDb}
          scale={kMixerFaderScale}
          label={t("mixer.fader_label", { name: channel.name })}
          onInput={onGainInput}
          onCommit={onGainCommit}
        />
      </div>
      <MuteButton
        muted={channel.channel_muted}
        text={t("mute.channel_muted")}
        label={t("mixer.mute_label", { name: channel.name })}
        pending={mutePending}
        onToggle={onToggleMute}
      />
      <div class="strip__footer">
        <button
          type="button"
          class={`strip__sel${selected ? " strip__sel--on" : ""}`}
          aria-pressed={selected}
          onClick={onSelect}
        >
          {t("mixer.sel")}
        </button>
        <button
          type="button"
          class={`strip__return${channel.return_muted ? "" : " strip__return--on"}`}
          aria-pressed={!channel.return_muted}
          aria-label={t("mixer.return_label", { name: channel.name })}
          title={t("mixer.return_hint")}
          disabled={mutePending}
          onClick={onToggleReturn}
        >
          {t("mixer.return")}
        </button>
        {channel.connection_state === "LOST" || channel.connection_state === "FAILED" ? (
          <button type="button" class="strip__remove" onClick={onRemove}>
            {t("mixer.remove_strip")}
          </button>
        ) : null}
      </div>
    </section>
  );
}
