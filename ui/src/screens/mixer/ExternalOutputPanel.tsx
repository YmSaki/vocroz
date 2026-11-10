// SPDX-License-Identifier: AGPL-3.0-only
// SRTP 外部出力の設定と、開始と停止、接続テストを行う非モーダルのサイドパネル。
// ミキシングを止めずに、外部の受信機へ送る設定を変えられるようにする（docs/adr/0040-srtp-external-output.md）。
//
// 設定の入力中の値はこのパネルだけが持つ下書きで、「設定を保存」でエンジンへ送る。表示する状態と結果は
// エンジンから届いた値だけを使う（エンジン主権）。鍵はエンジンから返ってこないので、入力欄は空から始め、
// 送ったら消す。作った鍵を受信機へ貼るための写しは、この入力欄の中にだけ置く。

import { useEffect, useState } from "preact/hooks";
import { t, type MessageKey } from "../../i18n/i18n";
import type {
  ExternalOutput,
  ExternalOutputConfig,
  ExternalOutputProfile,
  RoutingBus,
  SrtpSuite,
} from "../../ipc/messages";
import { generateSrtpKey } from "./srtp-key";
import "./ExternalOutputPanel.css";

/** 外部出力のパネルの props。 */
export interface ExternalOutputPanelProps {
  output: ExternalOutput;
  /** 送り出せるバス（適用中のルーティングのバス）。Master はパネルが先頭に足す。 */
  buses: readonly RoutingBus[];
  /** 外部出力のコマンドの ACK を待っているか。 */
  pending: boolean;
  onSaveConfig: (config: ExternalOutputConfig) => void;
  onSetKey: (key: string) => void;
  onSetRunning: (running: boolean) => void;
  onTest: () => void;
  /** 鍵を受信機へ貼るためにコピーする。 */
  onCopyKey: (key: string) => void;
  onClose: () => void;
  /** 鍵を作る。テストで差し替えられるようにする。 */
  generateKey?: () => string;
}

/** パネルの幅（px）。トーストをパネルに被せないよう、画面側でもこの値を使う。 */
export const kExternalOutputPanelWidthPx = 340;

/** Master の entity_id。送り出す元として、バスと同じ一覧で選べるようにする。 */
const kMasterSourceId = "master";

const kProfiles: readonly ExternalOutputProfile[] = ["OPUS_STEREO", "OPUS_MONO"];
const kSuites: readonly SrtpSuite[] = ["AES_CM_128_HMAC_SHA1_80", "AES_CM_128_HMAC_SHA1_32"];

/** 下書きの数値欄。入力途中の文字列を残せるよう、数値に直すのは保存のときにする。 */
interface Draft {
  host: string;
  port: string;
  source_id: string;
  profile: ExternalOutputProfile;
  payload_type: string;
  crypto_suite: SrtpSuite;
}

function draftOf(config: ExternalOutputConfig): Draft {
  return {
    ...config,
    port: String(config.port),
    payload_type: String(config.payload_type),
  };
}

/** 下書きを設定に直す。数値として読めなければ null（範囲の検証はエンジンが行う）。 */
function configOf(draft: Draft): ExternalOutputConfig | null {
  const port = Number(draft.port);
  const payloadType = Number(draft.payload_type);
  if (!Number.isInteger(port) || !Number.isInteger(payloadType)) return null;
  return { ...draft, host: draft.host.trim(), port, payload_type: payloadType };
}

function sameConfig(a: ExternalOutputConfig, b: ExternalOutputConfig): boolean {
  return (
    a.host === b.host &&
    a.port === b.port &&
    a.source_id === b.source_id &&
    a.profile === b.profile &&
    a.payload_type === b.payload_type &&
    a.crypto_suite === b.crypto_suite
  );
}

/** 外部出力のパネル。 */
export function ExternalOutputPanel({
  output,
  buses,
  pending,
  onSaveConfig,
  onSetKey,
  onSetRunning,
  onTest,
  onCopyKey,
  onClose,
  generateKey = generateSrtpKey,
}: ExternalOutputPanelProps) {
  const { config, state } = output;
  const [draft, setDraft] = useState<Draft>(() => draftOf(config));
  const [key, setKey] = useState("");
  const [showKey, setShowKey] = useState(false);
  // エンジンの設定が変わったら（保存の ACK のあとや、ほかの画面からの変更）、下書きを合わせる。
  useEffect(() => setDraft(draftOf(config)), [config]);

  const stopped = state === "STOPPED";
  const editable = stopped && !pending;
  const parsed = configOf(draft);
  const dirty = parsed === null || !sameConfig(parsed, config);
  // 保存していない変更があるときは始めない。画面の値と違う宛先へ送り出す誤りを防ぐため。
  const ready = config.host !== "" && output.key_configured && !dirty;
  const sourceKnown =
    draft.source_id === kMasterSourceId || buses.some((b) => b.entity_id === draft.source_id);
  const update = (patch: Partial<Draft>) => setDraft((current) => ({ ...current, ...patch }));

  return (
    <aside
      class="external-output-panel"
      style={{ width: `${kExternalOutputPanelWidthPx}px` }}
      aria-label={t("external_output.title")}
    >
      <header class="external-output-panel__header">
        <h2>{t("external_output.title")}</h2>
        <button type="button" class="external-output-panel__button" onClick={onClose}>
          {t("common.close")}
        </button>
      </header>
      <div class="external-output-panel__body">
        <p class="external-output-panel__hint">{t("external_output.hint")}</p>
        <dl class="external-output-panel__status">
          <dt>{t("external_output.state")}</dt>
          <dd class={`external-output-panel__state external-output-panel__state--${state}`}>
            {t(`external_output.state.${state}` as MessageKey)}
          </dd>
          <dt>{t("external_output.result")}</dt>
          <dd>
            {output.last_result === null
              ? t("external_output.result.none")
              : t(`external_output.result.${output.last_result}` as MessageKey)}
          </dd>
        </dl>
        <div class="external-output-panel__actions">
          {state === "RUNNING" ? (
            <button
              type="button"
              class="external-output-panel__button external-output-panel__stop"
              disabled={pending}
              onClick={() => onSetRunning(false)}
            >
              {t("external_output.stop")}
            </button>
          ) : (
            <button
              type="button"
              class="external-output-panel__button external-output-panel__start"
              disabled={!stopped || !ready || pending}
              onClick={() => onSetRunning(true)}
            >
              {t("external_output.start")}
            </button>
          )}
          <button
            type="button"
            class="external-output-panel__button"
            title={t("external_output.test_hint")}
            disabled={!stopped || !ready || pending}
            onClick={onTest}
          >
            {t("external_output.test")}
          </button>
        </div>
        <p class="external-output-panel__note">{t("external_output.result_note")}</p>

        <form
          class="external-output-panel__form"
          onSubmit={(event) => {
            event.preventDefault();
            if (parsed !== null) onSaveConfig(parsed);
          }}
        >
          <label>
            <span>{t("external_output.host")}</span>
            <input
              type="text"
              value={draft.host}
              placeholder={t("external_output.host_placeholder")}
              disabled={!editable}
              spellcheck={false}
              onInput={(e) => update({ host: e.currentTarget.value })}
            />
          </label>
          <label>
            <span>{t("external_output.port")}</span>
            <input
              type="number"
              min={1}
              max={65535}
              value={draft.port}
              disabled={!editable}
              onInput={(e) => update({ port: e.currentTarget.value })}
            />
          </label>
          <label>
            <span>{t("external_output.source")}</span>
            <select
              value={draft.source_id}
              disabled={!editable}
              onChange={(e) => update({ source_id: e.currentTarget.value })}
            >
              <option value={kMasterSourceId}>{t("external_output.source_master")}</option>
              {buses.map((bus) => (
                <option key={bus.entity_id} value={bus.entity_id}>
                  {bus.name}
                </option>
              ))}
              {sourceKnown ? null : (
                <option value={draft.source_id}>{t("external_output.source_missing")}</option>
              )}
            </select>
          </label>
          <label>
            <span>{t("external_output.profile")}</span>
            <select
              value={draft.profile}
              disabled={!editable}
              onChange={(e) => update({ profile: e.currentTarget.value as ExternalOutputProfile })}
            >
              {kProfiles.map((profile) => (
                <option key={profile} value={profile}>
                  {t(`external_output.profile.${profile}` as MessageKey)}
                </option>
              ))}
            </select>
          </label>
          <label>
            <span>{t("external_output.payload_type")}</span>
            <input
              type="number"
              min={96}
              max={127}
              value={draft.payload_type}
              disabled={!editable}
              onInput={(e) => update({ payload_type: e.currentTarget.value })}
            />
          </label>
          <label>
            <span>{t("external_output.suite")}</span>
            <select
              value={draft.crypto_suite}
              disabled={!editable}
              onChange={(e) => update({ crypto_suite: e.currentTarget.value as SrtpSuite })}
            >
              {kSuites.map((suite) => (
                <option key={suite} value={suite}>
                  {suite}
                </option>
              ))}
            </select>
          </label>
          <button
            type="submit"
            class="external-output-panel__button"
            disabled={!editable || !dirty || parsed === null}
          >
            {t("external_output.save")}
          </button>
        </form>

        <form
          class="external-output-panel__form"
          onSubmit={(event) => {
            event.preventDefault();
            const text = key.trim();
            if (text === "") return;
            onSetKey(text);
            // 送った鍵は画面に残さない。エンジンも鍵を UI へ返さない。
            setKey("");
            setShowKey(false);
          }}
        >
          <label>
            <span>{t("external_output.key")}</span>
            <input
              type={showKey ? "text" : "password"}
              value={key}
              placeholder={t("external_output.key_placeholder")}
              autocomplete="off"
              spellcheck={false}
              disabled={!editable}
              onInput={(e) => setKey(e.currentTarget.value)}
            />
          </label>
          <p class="external-output-panel__hint">
            {output.key_configured
              ? t("external_output.key_configured")
              : t("external_output.key_missing")}
          </p>
          <div class="external-output-panel__actions">
            <button
              type="button"
              class="external-output-panel__button"
              disabled={!editable}
              onClick={() => {
                setKey(generateKey());
                // 受信機へ貼り付けられるよう、作った鍵は見える状態にする。
                setShowKey(true);
              }}
            >
              {t("external_output.key_generate")}
            </button>
            <button
              type="button"
              class="external-output-panel__button"
              disabled={key === ""}
              onClick={() => setShowKey((show) => !show)}
            >
              {showKey ? t("external_output.key_hide") : t("external_output.key_show")}
            </button>
            <button
              type="button"
              class="external-output-panel__button"
              disabled={key === ""}
              onClick={() => onCopyKey(key.trim())}
            >
              {t("external_output.key_copy")}
            </button>
            <button
              type="submit"
              class="external-output-panel__button"
              disabled={!editable || key.trim() === ""}
            >
              {t("external_output.key_set")}
            </button>
          </div>
        </form>
      </div>
    </aside>
  );
}
