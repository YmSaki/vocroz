// SPDX-License-Identifier: AGPL-3.0-only
// 子機モードの状態を保持するストア。エンジン（子機ロジック）から届いた状態をそのまま保持する。

import type { ClientSnapshot, EngineToUiMessage } from "../ipc/messages";
import { Store } from "./store";

/** 子機モードの状態。スナップショットが届くまでは null。 */
export type ClientViewState = ClientSnapshot | null;

/** 子機モードの状態のストア。 */
export class ClientStore extends Store<ClientViewState> {
  constructor() {
    super(null);
  }

  /**
   * エンジンからのメッセージを取り込む。子機モードに関係しないメッセージは無視する。
   * @param message エンジンからのメッセージ
   */
  apply(message: EngineToUiMessage): void {
    switch (message.type) {
      case "client_snapshot":
        this.set(message.snapshot);
        return;
      case "client_updated":
        this.set(message.client);
        return;
      case "mixer_snapshot":
      case "session_updated":
      case "channel_updated":
      case "channel_removed":
      case "master_updated":
      case "pending_updated":
      case "ack":
      case "reject":
      case "gain_ack":
      case "meters":
      case "notice":
        return;
    }
  }
}
