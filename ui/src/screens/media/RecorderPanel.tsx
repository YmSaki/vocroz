// SPDX-License-Identifier: AGPL-3.0-only
// メディア画面の録音の区画。形式、保存先フォルダ、録る対象を選び、録音を開始と停止する。
// 表示する状態はエンジンから届いた Recorder だけを使い、操作はコマンドとして親へ渡す（エンジン主権）。
// 保存先フォルダの入力中の値だけは、この区画の下書きとして持つ。

import { useEffect, useState } from "preact/hooks";
import { t, type MessageKey } from "../../i18n/i18n";
import type { Recorder, RecorderConfig, RecorderResult, RecordingFormat } from "../../ipc/messages";
import { formatClock, toggleSource } from "./media-format";

/** 録る対象の候補。 */
export interface SourceOption {
  entityId: string;
  label: string;
}

/** 録音の区画の props。 */
export interface RecorderPanelProps {
  recorder: Recorder;
  /** 録る対象の候補（Master、適用中のバス、ストリップの順）。 */
  sources: readonly SourceOption[];
  /** 表示名を引く。候補にない対象（なくなったストリップなど）は entity_id で示す。 */
  labelOf: (entityId: string) => string;
  /** 録音のコマンドの ACK を待っているか。 */
  pending: boolean;
  onSetConfig: (config: RecorderConfig) => void;
  onSetRecording: (recording: boolean) => void;
}

const kFormats: readonly RecordingFormat[] = ["WAV", "FLAC"];

const kFormatKey: Record<RecordingFormat, MessageKey> = {
  WAV: "media.format.WAV",
  FLAC: "media.format.FLAC",
};

const kResultKey: Record<Exclude<RecorderResult, "OK">, MessageKey> = {
  DISK_FULL: "media.recorder_result.DISK_FULL",
  IO_ERROR: "media.recorder_result.IO_ERROR",
  FOLDER_UNAVAILABLE: "media.recorder_result.FOLDER_UNAVAILABLE",
};

/** 録音の区画。 */
export function RecorderPanel({
  recorder,
  sources,
  labelOf,
  pending,
  onSetConfig,
  onSetRecording,
}: RecorderPanelProps) {
  const { config } = recorder;
  const recording = recorder.state === "RECORDING";
  // 録音中は設定を変えられない（エンジンも INVALID_STATE で拒否する）。
  const locked = recording || pending;
  const [folder, setFolder] = useState(config.folder);
  // エンジンの値が変わったら下書きを合わせる（ほかの窓や設定の復元で変わることがある）。
  useEffect(() => setFolder(config.folder), [config.folder]);

  const dropped = recorder.tracks.filter((track) => track.dropped_ms > 0);
  const failure = recorder.last_result !== null && recorder.last_result !== "OK";

  return (
    <section class="media-section" aria-label={t("media.recorder")}>
      <h2>{t("media.recorder")}</h2>
      <div class="media-row">
        <span class="media-row__label">{t("media.format")}</span>
        {kFormats.map((format) => (
          <label key={format} class="media-choice">
            <input
              type="radio"
              name="recording-format"
              checked={config.format === format}
              disabled={locked}
              onChange={() => onSetConfig({ ...config, format })}
            />
            {t(kFormatKey[format])}
          </label>
        ))}
      </div>
      <form
        class="media-row"
        onSubmit={(e) => {
          e.preventDefault();
          onSetConfig({ ...config, folder: folder.trim() });
        }}
      >
        <label class="media-row__label" for="media-folder">
          {t("media.folder")}
        </label>
        <input
          id="media-folder"
          type="text"
          class="media-row__grow"
          value={folder}
          disabled={locked}
          onInput={(e) => setFolder(e.currentTarget.value)}
        />
        <button type="submit" disabled={locked || folder.trim() === config.folder}>
          {t("media.folder_apply")}
        </button>
      </form>
      <p class="media-section__hint">{t("media.folder_hint")}</p>
      <fieldset class="media-sources" disabled={locked}>
        <legend>{t("media.sources")}</legend>
        {sources.map((source) => (
          <label key={source.entityId} class="media-choice">
            <input
              type="checkbox"
              checked={config.sources.includes(source.entityId)}
              onChange={(e) =>
                onSetConfig({
                  ...config,
                  sources: toggleSource(config.sources, source.entityId, e.currentTarget.checked),
                })
              }
            />
            {source.label}
          </label>
        ))}
      </fieldset>
      <p class="media-section__hint">{t("media.sources_hint")}</p>
      <div class="media-row">
        <button
          type="button"
          class={recording ? "media-record media-record--on" : "media-record"}
          aria-pressed={recording}
          disabled={pending || (!recording && config.sources.length === 0)}
          onClick={() => onSetRecording(!recording)}
        >
          {t(recording ? "media.record_stop" : "media.record_start")}
        </button>
        <span class="media-record__time" role="timer" aria-label={t("media.elapsed")}>
          {formatClock(recorder.elapsed_ms)}
        </span>
        <span class="media-record__state" role="status">
          {recording
            ? t("media.recording", { take: recorder.take ?? "" })
            : recorder.take !== null
              ? t("media.last_take", { take: recorder.take })
              : t("media.not_recording")}
        </span>
      </div>
      {failure ? (
        <p class="media-section__error" role="alert">
          {t(kResultKey[recorder.last_result as Exclude<RecorderResult, "OK">])}
        </p>
      ) : null}
      {recorder.tracks.length > 0 ? (
        <ul class="media-tracks">
          {recorder.tracks.map((track) => (
            <li key={track.file_name}>
              <span>{track.file_name}</span>
              <span class="media-tracks__source">{labelOf(track.source_id)}</span>
            </li>
          ))}
        </ul>
      ) : null}
      {dropped.map((track) => (
        <p key={track.file_name} class="media-section__warning" role="alert">
          {t("media.dropped", { file: track.file_name, ms: track.dropped_ms })}
        </p>
      ))}
    </section>
  );
}
