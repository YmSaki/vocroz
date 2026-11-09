// SPDX-License-Identifier: AGPL-3.0-only
// 適用中のバスとセンドのフェーダーとミュート。ミキサー画面のストリップと同じ部品で、縦に並べる。
// レベルはルーティング全体とは別の entity なので、ここでの操作はステージング中の編集と衝突しない
// （docs/adr/0026-routing-staging.md）。

import { Fader } from "../../components/fader/Fader";
import { kMixerFaderScale } from "../../components/fader/fader-scale";
import { MuteButton } from "../../components/mute-button/MuteButton";
import { t } from "../../i18n/i18n";
import type { GainDb, RouteLevel } from "../../ipc/messages";

/** レベルの一覧の props。 */
export interface RouteLevelsViewProps {
  levels: readonly RouteLevel[];
  labelOf: (entityId: string) => string;
  gainOf: (level: RouteLevel) => GainDb;
  isPending: (entityId: string) => boolean;
  onGainInput: (entityId: string, value: GainDb) => void;
  onGainCommit: (entityId: string, value: GainDb) => void;
  onToggleMute: (level: RouteLevel) => void;
}

/** 適用中のバスとセンドのレベル。 */
export function RouteLevelsView({
  levels,
  labelOf,
  gainOf,
  isPending,
  onGainInput,
  onGainCommit,
  onToggleMute,
}: RouteLevelsViewProps) {
  return (
    <section class="route-levels" aria-label={t("routing.levels")}>
      <h2 class="route-levels__title">{t("routing.levels")}</h2>
      {levels.length === 0 ? (
        <p class="routing-section__empty">{t("routing.levels_empty")}</p>
      ) : (
        <div class="route-levels__strips">
          {levels.map((level) => {
            const name = labelOf(level.entity_id);
            return (
              <div
                key={level.entity_id}
                class="route-level"
                role="group"
                aria-label={name}
                data-entity-id={level.entity_id}
              >
                <div class="route-level__name" title={name}>
                  {name}
                </div>
                <Fader
                  value={gainOf(level)}
                  scale={kMixerFaderScale}
                  label={t("mixer.fader_label", { name })}
                  onInput={(value) => onGainInput(level.entity_id, value)}
                  onCommit={(value) => onGainCommit(level.entity_id, value)}
                />
                <MuteButton
                  muted={level.muted}
                  text={t("mute.channel_muted")}
                  label={t("mixer.mute_label", { name })}
                  pending={isPending(level.entity_id)}
                  onToggle={() => onToggleMute(level)}
                />
              </div>
            );
          })}
        </div>
      )}
    </section>
  );
}
