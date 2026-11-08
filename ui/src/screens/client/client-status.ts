// SPDX-License-Identifier: AGPL-3.0-only
// 子機の接続状態から、画面のどの部分を出すかを決める。
// 状態の値はエンジン（子機ロジック）が決め、ここでは表示の切り替え方だけを決める。

import type { ClientState } from "../../ipc/messages";

/** 接続の欄に出す内容。 */
export type ConnectionView = "invite" | "waiting" | "connected";

/**
 * 接続の欄に出す内容を選ぶ。
 * 未接続と失敗は招待の入力、接続処理中は待機と取り消し、接続中は接続先と退出を出す。
 * @param state 子機の接続状態
 */
export function connectionView(state: ClientState): ConnectionView {
  switch (state) {
    case "DISCONNECTED":
    case "FAILED":
      return "invite";
    case "RESOLVING":
    case "AUTHENTICATING":
    case "WAITING_APPROVAL":
    case "CONNECTING":
      return "waiting";
    case "CONNECTED":
    case "RECONNECTING":
      return "connected";
  }
}
