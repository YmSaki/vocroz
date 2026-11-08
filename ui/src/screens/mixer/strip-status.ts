// SPDX-License-Identifier: AGPL-3.0-only
// チャンネルストリップの状態欄に出す表示ラベルを選ぶ。
//
// 状態欄には次の優先順位で 1 つだけ出す。
// FAILED > LOST > RECONNECTING > CONNECTING > NO_SIGNAL > （ラベルなし: RECEIVING / PAUSED / RESUMING）
// ミュートはこの欄に出さず、別のバッジとボタンで常に表示する。
// 状態の値はエンジンが決めたものをそのまま使い、ここでは表示の選び方だけを決める。

import type { MessageKey } from "../../i18n/i18n";
import type { ChannelStrip } from "../../ipc/messages";

/** 状態欄の表示。 */
export interface StripStatus {
  /** 表示ラベルのキー。ラベルを出さないときは null。 */
  key: MessageKey | null;
  /** 警告として目立たせるか。 */
  warning: boolean;
}

/**
 * ストリップの状態欄の表示を選ぶ。
 * @param channel ストリップの状態
 */
export function stripStatus(channel: ChannelStrip): StripStatus {
  switch (channel.connection_state) {
    case "FAILED":
      return { key: "connection.FAILED", warning: true };
    case "LOST":
      return { key: "connection.LOST", warning: true };
    case "RECONNECTING":
      return { key: "connection.RECONNECTING", warning: true };
    case "CONNECTING":
      return { key: "connection.CONNECTING", warning: false };
    case "CONNECTED":
      switch (channel.signal_state) {
        case "NO_SIGNAL":
          return { key: "signal.NO_SIGNAL", warning: true };
        case "RECEIVING":
        case "PAUSED":
        case "RESUMING":
        case null:
          return { key: null, warning: false };
      }
  }
}

/**
 * ストリップが警告の件数に数える状態か。ステータス帯の警告件数に使う。
 * @param channel ストリップの状態
 */
export function isStripWarning(channel: ChannelStrip): boolean {
  return stripStatus(channel).warning;
}
