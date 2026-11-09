// SPDX-License-Identifier: AGPL-3.0-only
import { describe, expect, it } from "vitest";
import { FakeMixerEngine } from "../ipc/fake-engine/fake-mixer-engine";
import type { ChannelStrip } from "../ipc/messages";
import { MixerStore } from "./mixer-store";

function snapshot() {
  return new FakeMixerEngine({ meterIntervalMs: 0, scenario: "demo" }).snapshot();
}

describe("MixerStore", () => {
  it("スナップショットの前に届いた差分は捨てる", () => {
    const store = new MixerStore();
    store.apply({ type: "master_updated", master: snapshot().master });
    expect(store.get()).toBeNull();
  });

  it("既存のストリップは位置を変えずに更新し、新しいストリップは末尾に足す", () => {
    const store = new MixerStore();
    const initial = snapshot();
    store.apply({ type: "mixer_snapshot", snapshot: initial });
    const second = initial.channels[1] as ChannelStrip;
    store.apply({ type: "channel_updated", channel: { ...second, channel_muted: false } });
    const added: ChannelStrip = { ...second, entity_id: "new", name: "New" };
    store.apply({ type: "channel_updated", channel: added });
    const ids = store.get()?.channels.map((c) => c.entity_id);
    expect(ids).toEqual([...initial.channels.map((c) => c.entity_id), "new"]);
    expect(store.get()?.channels[1]?.channel_muted).toBe(false);
  });

  it("削除されたストリップを取り除く", () => {
    const store = new MixerStore();
    store.apply({ type: "mixer_snapshot", snapshot: snapshot() });
    store.apply({ type: "channel_removed", entity_id: "ch1" });
    expect(store.get()?.channels.some((c) => c.entity_id === "ch1")).toBe(false);
  });

  it("ルーティングの更新とバスやセンドのレベルの更新を取り込む", () => {
    const store = new MixerStore();
    const initial = snapshot();
    store.apply({ type: "mixer_snapshot", snapshot: initial });
    const level = { entity_id: "send-1", revision: 1, gain_db: 0, muted: true };
    store.apply({
      type: "routing_updated",
      routing: { ...initial.routing, revision: 3, levels: [level] },
    });
    store.apply({
      type: "route_level_updated",
      level: { ...level, revision: 2, muted: false },
    });
    expect(store.get()?.routing.revision).toBe(3);
    expect(store.get()?.routing.levels).toEqual([{ ...level, revision: 2, muted: false }]);
  });
});
