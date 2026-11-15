// SPDX-License-Identifier: AGPL-3.0-only
// フェイクエンジンのプラグインの一覧、スキャン、挿入の状態。pa-engine の session（PluginStatusBook、
// PluginInsertBook、HostCommandHandler）を簡略化して、一覧のページ分け、再スキャン、挿入の読み込みの状態、
// エディターを開くコマンドの検証を再現する。
// 規則の正本は C++ 側（modules/session/src/plugin_status_book.cpp、docs/adr/0042-plugin-insert.md）で、
// ここは画面の開発と手動の確認のための写しである。プラグインは実際には読み込まず、一覧にあれば
// すぐに使える状態にする。

import type {
  BlockedPluginFile,
  ErrorCode,
  EngineToUiMessage,
  PluginEntry,
  PluginInsertStatus,
  PluginsInfo,
  RoutingInsert,
} from "../messages";

/** 1 ページの件数。control-protocol の kMaxPluginCatalogPageEntries と同じ値。 */
export const kFakePluginPageEntries = 24;

/** entity "plugins" の revision。状態を変えるコマンドがないので 1 のまま変えない。 */
const kPluginsRevision = 1;

/** 開発用の一覧。VSTi は挿入にできないことを画面で確かめられるよう 1 つ混ぜる。 */
function demoCatalog(): PluginEntry[] {
  return [
    {
      plugin_id: "VST3-DemoEQ",
      name: "Demo EQ",
      vendor: "VOCROZ",
      version: "1.0.0",
      is_instrument: false,
    },
    {
      plugin_id: "VST3-DemoComp",
      name: "Demo Compressor",
      vendor: "VOCROZ",
      version: "1.0.0",
      is_instrument: false,
    },
    {
      plugin_id: "VST3-DemoReverb",
      name: "Demo Reverb",
      vendor: "VOCROZ",
      version: "1.2.0",
      is_instrument: false,
    },
    {
      plugin_id: "VST3-DemoSynth",
      name: "Demo Synth",
      vendor: "VOCROZ",
      version: "0.9.0",
      is_instrument: true,
    },
  ];
}

/** フェイクエンジンのプラグインの状態。 */
export class FakePlugins {
  private catalog: PluginEntry[] = demoCatalog();
  private blocked: BlockedPluginFile[] = [];
  private generation = 1;
  private scanning = false;
  /** 処理中に失敗した挿入。読み込み直す（entity_id を変える）まで失敗のまま。 */
  private readonly failed = new Set<string>();

  /** @param entityId entity "plugins" の entity_id */
  constructor(readonly entityId: string) {}

  /** entity "plugins" の revision。 */
  revision(): number {
    return kPluginsRevision;
  }

  /**
   * スキャンと挿入の状態。
   * @param inserts 適用中のルーティングの挿入
   */
  info(inserts: readonly RoutingInsert[]): PluginsInfo {
    return {
      entity_id: this.entityId,
      revision: kPluginsRevision,
      scanning: this.scanning,
      catalog_generation: this.generation,
      catalog_size: this.catalog.length,
      blocked: this.blocked,
      inserts: inserts.map((i) => this.statusOf(i)),
    };
  }

  /** 一覧のページ。Hello と一覧が変わったときに、plugins_updated のあとに送る。 */
  pages(): EngineToUiMessage[] {
    const pages: EngineToUiMessage[] = [];
    for (let offset = 0; offset < this.catalog.length; offset += kFakePluginPageEntries) {
      pages.push({
        type: "plugin_catalog_page",
        generation: this.generation,
        offset,
        plugins: this.catalog.slice(offset, offset + kFakePluginPageEntries),
      });
    }
    return pages;
  }

  /** 再スキャンを始める。スキャン中にする。 */
  startScan(): void {
    this.scanning = true;
  }

  /**
   * スキャンを終える。新しい一覧の世代に進める。
   * @param blocked スキャンから外したファイル
   */
  finishScan(blocked: BlockedPluginFile[] = []): void {
    this.scanning = false;
    this.blocked = blocked;
    this.catalog = demoCatalog();
    this.generation += 1;
  }

  /** 挿入の処理中の失敗を再現する。 */
  fail(insertId: string): void {
    this.failed.add(insertId);
  }

  /**
   * エディターを開けるかを確かめる。開けるなら null。
   * @param inserts 適用中のルーティングの挿入
   */
  checkEditor(inserts: readonly RoutingInsert[], insertId: string): ErrorCode | null {
    const insert = inserts.find((i) => i.entity_id === insertId);
    if (insert === undefined) return "NOT_FOUND";
    return this.statusOf(insert).state === "READY" ? null : "INVALID_STATE";
  }

  /** プラグインの表示名。一覧になければ識別子。 */
  nameOf(pluginId: string): string {
    return this.catalog.find((p) => p.plugin_id === pluginId)?.name ?? pluginId;
  }

  private statusOf(insert: RoutingInsert): PluginInsertStatus {
    const entry = this.catalog.find((p) => p.plugin_id === insert.plugin_id);
    if (entry === undefined || entry.is_instrument) {
      return { entity_id: insert.entity_id, state: "FAILED", failure: "NOT_FOUND" };
    }
    if (this.failed.has(insert.entity_id)) {
      return { entity_id: insert.entity_id, state: "FAILED", failure: "PROCESSING_FAILED" };
    }
    return { entity_id: insert.entity_id, state: "READY", failure: null };
  }
}
