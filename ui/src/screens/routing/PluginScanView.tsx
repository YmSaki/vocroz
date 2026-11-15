// SPDX-License-Identifier: AGPL-3.0-only
// プラグインのスキャンの状態の欄。スキャン中か、使えるプラグインの数、読み込み不可にしたファイルを出し、
// 再スキャンを依頼する。読み込み不可のファイルは、スキャンで落ちたり止まったりしたファイルで、
// ファイルを更新するまでスキャンから外している（docs/adr/0032-plugin-catalog-and-loader.md）。

import { t, type MessageKey } from "../../i18n/i18n";
import type { BlockedPluginReason, PluginsInfo } from "../../ipc/messages";

/** スキャンの状態の欄の props。 */
export interface PluginScanViewProps {
  /** スキャンと挿入の状態。届くまでは null。 */
  info: PluginsInfo | null;
  /** 再スキャンの依頼を送っている間は true。 */
  disabled: boolean;
  onRescan: () => void;
}

const kReasonKey: Record<BlockedPluginReason, MessageKey> = {
  CRASHED: "plugin.blocked.CRASHED",
  TIMED_OUT: "plugin.blocked.TIMED_OUT",
  INVALID_OUTPUT: "plugin.blocked.INVALID_OUTPUT",
};

/** プラグインのスキャンの状態の欄。 */
export function PluginScanView({ info, disabled, onRescan }: PluginScanViewProps) {
  const scanning = info?.scanning === true;
  const summary =
    info === null
      ? t("plugin.scan_unknown")
      : scanning
        ? t("plugin.scanning")
        : t("plugin.scan_summary", { count: info.catalog_size });
  return (
    <section class="routing-section" aria-label={t("plugin.scan_title")}>
      <header class="routing-section__header">
        <h2>{t("plugin.scan_title")}</h2>
        <button type="button" disabled={disabled || info === null || scanning} onClick={onRescan}>
          {t("plugin.rescan")}
        </button>
      </header>
      <p class="routing-section__hint" aria-live="polite">
        {summary}
      </p>
      {info !== null && info.blocked.length > 0 ? (
        <ul class="plugin-blocked" aria-label={t("plugin.blocked_title")}>
          {info.blocked.map((file) => (
            <li key={file.path}>
              <span class="plugin-blocked__path">{file.path}</span>
              <span class="plugin-blocked__reason">{t(kReasonKey[file.reason])}</span>
            </li>
          ))}
        </ul>
      ) : null}
    </section>
  );
}
