// SPDX-License-Identifier: AGPL-3.0-only
// プラグインのスキャンと挿入の状態、プラグインの一覧を保持するストア。
// エンジンはこれらをスナップショットに入れず、plugins_updated と、24 件ずつのページ
// （plugin_catalog_page）で送る。メッセージ 1 通の上限（64 KiB）に一覧が収まらないため
// （docs/adr/0042-plugin-insert.md）。ここではページを世代ごとに集めるだけで、状態は導出しない。

import type { EngineToUiMessage, PluginEntry, PluginsInfo } from "../ipc/messages";
import { Store } from "./store";

/** プラグインの状態。 */
export interface PluginState {
  /** スキャンと挿入の状態。plugins_updated が届くまでは null。 */
  info: PluginsInfo | null;
  /**
   * 今の世代の一覧。ページがそろうまでは、届いたページの分だけを並べる（届いていない位置は詰める）。
   * 一覧が途中でも、挿入の名前を出せるものから出すため。
   */
  catalog: readonly PluginEntry[];
  /** 今の世代の一覧が catalog_size 件そろったか。 */
  complete: boolean;
}

/** プラグインの状態のストア。 */
export class PluginStore extends Store<PluginState> {
  /** 集めているページの世代。 */
  private generation: number | null = null;
  /** 世代のページの中身。添字は一覧の中の位置。 */
  private slots: (PluginEntry | undefined)[] = [];

  constructor() {
    super({ info: null, catalog: [], complete: false });
  }

  /**
   * エンジンからのメッセージを取り込む。プラグインに関係しないメッセージは無視する。
   * @param message エンジンからのメッセージ
   */
  apply(message: EngineToUiMessage): void {
    if (message.type === "plugins_updated") {
      // 世代が変わったら、古い世代のページを捨てる。新しい一覧と混ざらないようにするため。
      // ページが plugins_updated より先に届いていれば、その世代のページは残す。
      this.collect(message.plugins.catalog_generation);
      this.publish(message.plugins);
      return;
    }
    if (message.type === "plugin_catalog_page") {
      const info = this.get().info;
      // 今の状態より古い世代のページは、もう使わない。
      if (info !== null && message.generation < info.catalog_generation) return;
      this.collect(message.generation);
      message.plugins.forEach((entry, i) => {
        this.slots[message.offset + i] = entry;
      });
      this.publish(info);
    }
  }

  /**
   * 識別子からプラグインを引く。一覧になければ undefined。
   * @param pluginId プラグインの識別子
   */
  find(pluginId: string): PluginEntry | undefined {
    return this.get().catalog.find((p) => p.plugin_id === pluginId);
  }

  private collect(generation: number): void {
    if (this.generation === generation) return;
    this.generation = generation;
    this.slots = [];
  }

  private publish(info: PluginsInfo | null): void {
    const usable = info !== null && info.catalog_generation === this.generation;
    const size = usable ? info.catalog_size : 0;
    const catalog = usable
      ? this.slots.slice(0, size).filter((p): p is PluginEntry => p !== undefined)
      : [];
    this.set({ info, catalog, complete: usable && catalog.length === size });
  }
}
