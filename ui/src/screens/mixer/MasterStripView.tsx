// SPDX-License-Identifier: AGPL-3.0-only
// 右端に固定する Master ストリップ。
// エンジンが安全のために設定したミュート（SAFETY）は、オペレーターのミュートとラベルで区別する。

import { Fader } from "../../components/fader/Fader";
import { kMixerFaderScale } from "../../components/fader/fader-scale";
import { Meter, type MeterSample } from "../../components/meter/Meter";
import { MuteButton } from "../../components/mute-button/MuteButton";
import { t } from "../../i18n/i18n";
import type { GainDb, MasterStrip } from "../../ipc/messages";

/** Master ストリップの props。 */
export interface MasterStripViewProps {
  master: MasterStrip;
  gainDb: GainDb;
  mutePending: boolean;
  readMeter: () => MeterSample | undefined;
  onGainInput: (value: GainDb) => void;
  onGainCommit: (value: GainDb) => void;
  onToggleMute: () => void;
}

/** Master ストリップの幅（px）。トーストを Master に被せないため、画面側でもこの値を使う。 */
export const kMasterStripWidthPx = 128;

/** Master ストリップ。 */
export function MasterStripView({
  master,
  gainDb,
  mutePending,
  readMeter,
  onGainInput,
  onGainCommit,
  onToggleMute,
}: MasterStripViewProps) {
  const safety = master.master_muted && master.master_mute_reason === "SAFETY";
  return (
    <section
      class="strip strip--master"
      style={{ width: `${kMasterStripWidthPx}px` }}
      aria-label={t("mixer.master")}
      data-entity-id={master.entity_id}
    >
      <header class="strip__header">
        <div class="strip__name">{t("mixer.master")}</div>
      </header>
      <div class={`strip__status${safety ? " strip__status--warning" : ""}`}>
        {safety ? t("mute.safety_hint_short") : ""}
      </div>
      <div class="strip__body">
        <Meter read={readMeter} label={t("mixer.meter_label", { name: t("mixer.master") })} />
        <Fader
          value={gainDb}
          scale={kMixerFaderScale}
          label={t("mixer.fader_label", { name: t("mixer.master") })}
          onInput={onGainInput}
          onCommit={onGainCommit}
        />
      </div>
      <MuteButton
        muted={master.master_muted}
        text={safety ? t("mute.safety_muted") : t("mute.master_muted")}
        label={t("mixer.mute_label", { name: t("mixer.master") })}
        safety={safety}
        pending={mutePending}
        onToggle={onToggleMute}
      />
    </section>
  );
}
