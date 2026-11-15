// SPDX-License-Identifier: AGPL-3.0-only
// メディア画面のプレイヤーの区画。メディアフォルダのライブラリからファイルを選び、流す先を決めて再生する。
// 再生できるのは一覧のファイルだけで、任意のパスは選べない（docs/adr/0043-recording-and-media-player.md）。
// シークバーはドラッグ中だけこの区画が値を持ち、離したときに位置をエンジンへ送る。

import { useState } from "preact/hooks";
import { t, type MessageKey } from "../../i18n/i18n";
import type { MediaFile, MediaPlayer, MediaResult, MediaTransport } from "../../ipc/messages";
import { formatClock } from "./media-format";
import type { SourceOption } from "./RecorderPanel";

/** プレイヤーの区画の props。 */
export interface PlayerPanelProps {
  player: MediaPlayer;
  library: readonly MediaFile[];
  /** 流す先の候補（Master と適用中のバス）。 */
  targets: readonly SourceOption[];
  /** プレイヤーのコマンドの ACK を待っているか。 */
  pending: boolean;
  onLoad: (path: string) => void;
  onTransport: (transport: MediaTransport) => void;
  onSeek: (positionMs: number) => void;
  onSetTarget: (target: string) => void;
  onRefresh: () => void;
}

const kResultKey: Record<Exclude<MediaResult, "OK">, MessageKey> = {
  NOT_FOUND: "media.player_result.NOT_FOUND",
  UNSUPPORTED_FORMAT: "media.player_result.UNSUPPORTED_FORMAT",
  IO_ERROR: "media.player_result.IO_ERROR",
};

/** プレイヤーの区画。 */
export function PlayerPanel({
  player,
  library,
  targets,
  pending,
  onLoad,
  onTransport,
  onSeek,
  onSetTarget,
  onRefresh,
}: PlayerPanelProps) {
  // ドラッグ中のシークバーの値。離すまでは位置の通知で戻さない。
  const [dragMs, setDragMs] = useState<number | null>(null);
  const loaded = player.file !== null;
  const position = dragMs ?? player.position_ms;
  const failure = player.last_result !== null && player.last_result !== "OK";
  // 流す先がいまのルーティングにない（バスを消した）ときも、選択肢に残して見えるようにする。
  const targetOptions = targets.some((o) => o.entityId === player.target)
    ? targets
    : [...targets, { entityId: player.target, label: player.target }];

  return (
    <section class="media-section" aria-label={t("media.player")}>
      <div class="media-section__header">
        <h2>{t("media.player")}</h2>
        <button type="button" disabled={pending} onClick={onRefresh}>
          {t("media.refresh")}
        </button>
      </div>
      {library.length === 0 ? (
        <p class="media-section__hint">{t("media.library_empty")}</p>
      ) : (
        <ul class="media-library" aria-label={t("media.library")}>
          {library.map((file) => (
            <li key={file.path}>
              <button
                type="button"
                class={
                  file.path === player.file
                    ? "media-library__item media-library__item--loaded"
                    : "media-library__item"
                }
                aria-current={file.path === player.file}
                disabled={pending}
                onClick={() => onLoad(file.path)}
              >
                {file.path}
              </button>
            </li>
          ))}
        </ul>
      )}
      <div class="media-row">
        <span class="media-row__label">{t("media.file")}</span>
        <span class="media-row__grow media-player__file">{player.file ?? t("media.no_file")}</span>
        <button type="button" disabled={!loaded || pending} onClick={() => onLoad("")}>
          {t("media.unload")}
        </button>
      </div>
      <div class="media-row">
        <label class="media-row__label" for="media-target">
          {t("media.target")}
        </label>
        <select
          id="media-target"
          value={player.target}
          disabled={pending}
          onChange={(e) => onSetTarget(e.currentTarget.value)}
        >
          {targetOptions.map((option) => (
            <option key={option.entityId} value={option.entityId}>
              {option.label}
            </option>
          ))}
        </select>
      </div>
      <div class="media-row">
        <button
          type="button"
          class="media-transport__play"
          aria-pressed={player.transport === "PLAYING"}
          disabled={!loaded || pending || player.transport === "PLAYING"}
          onClick={() => onTransport("PLAYING")}
        >
          {t("media.play")}
        </button>
        <button
          type="button"
          disabled={!loaded || pending || player.transport !== "PLAYING"}
          onClick={() => onTransport("PAUSED")}
        >
          {t("media.pause")}
        </button>
        <button
          type="button"
          disabled={!loaded || pending || (player.transport === "STOPPED" && position === 0)}
          onClick={() => onTransport("STOPPED")}
        >
          {t("media.stop")}
        </button>
        <span class="media-player__time" role="timer" aria-label={t("media.seek")}>
          {t("media.position", {
            position: formatClock(position),
            duration: formatClock(player.duration_ms),
          })}
        </span>
      </div>
      <input
        type="range"
        class="media-seek"
        aria-label={t("media.seek")}
        min={0}
        max={Math.max(player.duration_ms, 0)}
        step={100}
        value={position}
        disabled={!loaded || pending}
        onInput={(e) => setDragMs(Number(e.currentTarget.value))}
        onChange={(e) => {
          setDragMs(null);
          onSeek(Number(e.currentTarget.value));
        }}
      />
      {failure ? (
        <p class="media-section__error" role="alert">
          {t(kResultKey[player.last_result as Exclude<MediaResult, "OK">])}
        </p>
      ) : null}
    </section>
  );
}
