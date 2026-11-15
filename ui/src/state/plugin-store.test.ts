// SPDX-License-Identifier: AGPL-3.0-only
// プラグインの状態のストア。一覧は世代ごとのページで届き、ストアは今の世代のページだけを集める。
import { describe, expect, it } from "vitest";
import type { PluginEntry, PluginsInfo } from "../ipc/messages";
import { PluginStore } from "./plugin-store";

function info(generation: number, size: number): PluginsInfo {
  return {
    entity_id: "plugins",
    revision: 1,
    scanning: false,
    catalog_generation: generation,
    catalog_size: size,
    blocked: [],
    inserts: [],
  };
}

function entry(id: string): PluginEntry {
  return { plugin_id: id, name: id, vendor: "", version: "", is_instrument: false };
}

describe("PluginStore", () => {
  it("同じ世代のページがそろったら一覧を完成とする", () => {
    const store = new PluginStore();
    store.apply({ type: "plugins_updated", plugins: info(1, 3) });
    store.apply({ type: "plugin_catalog_page", generation: 1, offset: 2, plugins: [entry("c")] });
    expect(store.get().catalog.map((p) => p.plugin_id)).toEqual(["c"]);
    expect(store.get().complete).toBe(false);
    store.apply({
      type: "plugin_catalog_page",
      generation: 1,
      offset: 0,
      plugins: [entry("a"), entry("b")],
    });
    expect(store.get().catalog.map((p) => p.plugin_id)).toEqual(["a", "b", "c"]);
    expect(store.get().complete).toBe(true);
    expect(store.find("b")?.plugin_id).toBe("b");
  });

  it("世代が変わったら古い世代のページを捨て、遅れて届いた古いページも混ぜない", () => {
    const store = new PluginStore();
    store.apply({ type: "plugins_updated", plugins: info(1, 1) });
    store.apply({ type: "plugin_catalog_page", generation: 1, offset: 0, plugins: [entry("old")] });
    store.apply({ type: "plugins_updated", plugins: info(2, 1) });
    expect(store.get().catalog).toEqual([]);
    store.apply({ type: "plugin_catalog_page", generation: 1, offset: 0, plugins: [entry("old")] });
    expect(store.get().catalog).toEqual([]);
    store.apply({ type: "plugin_catalog_page", generation: 2, offset: 0, plugins: [entry("new")] });
    expect(store.get().catalog.map((p) => p.plugin_id)).toEqual(["new"]);
  });

  it("状態より先に届いたページは、同じ世代の状態が届いたときに使う", () => {
    const store = new PluginStore();
    store.apply({ type: "plugin_catalog_page", generation: 3, offset: 0, plugins: [entry("a")] });
    expect(store.get().info).toBeNull();
    store.apply({ type: "plugins_updated", plugins: info(3, 1) });
    expect(store.get().catalog.map((p) => p.plugin_id)).toEqual(["a"]);
    expect(store.get().complete).toBe(true);
  });
});
