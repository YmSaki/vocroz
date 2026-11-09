// SPDX-License-Identifier: AGPL-3.0-only
// 親機モードの状態を保持するストア。
// エンジンから届いたスナップショットと差分イベントをそのまま保持するだけで、状態を導出しない
// （エンジン主権。AGENTS.md 第 12 節）。

import type { EngineToUiMessage, MixerSnapshot } from "../ipc/messages";
import { Store } from "./store";

/** 親機モードの状態。スナップショットが届くまでは null。 */
export type MixerState = MixerSnapshot | null;

/** 親機モードの状態のストア。 */
export class MixerStore extends Store<MixerState> {
  constructor() {
    super(null);
  }

  /**
   * エンジンからのメッセージを取り込む。親機モードに関係しないメッセージは無視する。
   * @param message エンジンからのメッセージ
   */
  apply(message: EngineToUiMessage): void {
    if (message.type === "mixer_snapshot") {
      this.set(message.snapshot);
      return;
    }
    const current = this.get();
    // スナップショットより前に届いた差分は、適用先がないので捨てる（直後のスナップショットに含まれる）。
    if (current === null) return;
    switch (message.type) {
      case "session_updated":
        this.set({ ...current, session: message.session });
        return;
      case "channel_updated": {
        const index = current.channels.findIndex((c) => c.entity_id === message.channel.entity_id);
        // 新しいストリップは末尾に足す。既存のストリップの位置は変えない（本番中に並びを変えないため）。
        const channels =
          index < 0
            ? [...current.channels, message.channel]
            : current.channels.map((c, i) => (i === index ? message.channel : c));
        this.set({ ...current, channels });
        return;
      }
      case "channel_removed":
        this.set({
          ...current,
          channels: current.channels.filter((c) => c.entity_id !== message.entity_id),
        });
        return;
      case "master_updated":
        this.set({ ...current, master: message.master });
        return;
      case "pending_updated":
        this.set({ ...current, pending: message.pending });
        return;
      case "routing_updated":
        this.set({ ...current, routing: message.routing });
        return;
      case "route_level_updated": {
        const levels = current.routing.levels.map((l) =>
          l.entity_id === message.level.entity_id ? message.level : l,
        );
        this.set({ ...current, routing: { ...current.routing, levels } });
        return;
      }
      case "client_snapshot":
      case "client_updated":
      case "ack":
      case "reject":
      case "gain_ack":
      case "meters":
      case "link_stats":
      case "notice":
        return;
    }
  }
}
