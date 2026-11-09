// SPDX-License-Identifier: AGPL-3.0-only
// チャンネルストリップに出す入力の表示ラベル（マイク 2、アプリ音声、ステレオ）を作る。
//
// 1 台の子機は複数の入力を送れ、入力ごとに別のストリップになる（docs/adr/0035-client-input-expansion.md）。
// 同じ名前のストリップを見分けられるよう、入力の種類と番号とステレオかを表示する。
// モノラルのマイク 1 本（Alpha からの標準の形）のときは、表示を増やさないようラベルを出さない。
// 値はエンジンが決めたものをそのまま使い、ここでは表示の文字列だけを組み立てる。

import { t } from "../../i18n/i18n";
import type { ChannelStrip } from "../../ipc/messages";

/**
 * ストリップの入力の表示ラベルを返す。
 * @param channel ストリップの状態
 * @returns ラベル。モノラルの 1 本目のマイクなら空文字列
 */
export function stripInputLabel(
  channel: Pick<ChannelStrip, "source_kind" | "channel_count" | "source_number">,
): string {
  const stereo = channel.channel_count >= 2;
  if (channel.source_kind === "MICROPHONE" && channel.source_number <= 1 && !stereo) {
    return "";
  }
  const parts: string[] = [];
  switch (channel.source_kind) {
    case "MICROPHONE":
      parts.push(
        channel.source_number <= 1
          ? t("mixer.input_microphone")
          : t("mixer.input_microphone_numbered", { number: String(channel.source_number) }),
      );
      break;
    case "APP_AUDIO":
      parts.push(t("mixer.input_app_audio"));
      break;
  }
  if (stereo) parts.push(t("mixer.input_stereo"));
  return parts.join(" · ");
}
