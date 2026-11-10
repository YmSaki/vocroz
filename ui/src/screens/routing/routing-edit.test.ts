// SPDX-License-Identifier: AGPL-3.0-only
import { describe, expect, it } from "vitest";
import type { RoutingConfig, RoutingInfo } from "../../ipc/messages";
import { addOutput, nextRoutingId, removeNode } from "./routing-edit";

function empty(): RoutingConfig {
  return { buses: [], outputs: [], sends: [], delays: [], input_delays: [], inserts: [] };
}

function info(applied: RoutingConfig): RoutingInfo {
  return {
    entity_id: "routing",
    revision: 1,
    applied,
    feedback_approved: false,
    staged: null,
    levels: applied.buses.map((b) => ({
      entity_id: b.entity_id,
      revision: 1,
      gain_db: 0,
      muted: false,
    })),
  };
}

describe("nextRoutingId", () => {
  it("適用中の構成で使っている ID を、編集中に消しても使い直さない", () => {
    const applied = {
      ...empty(),
      buses: [{ entity_id: "bus-1", name: "A", kind: "USER" as const }],
    };
    const editing = removeNode(applied, "bus-1");
    // bus-1 を使い直すと、エンジンが前のバスのミュートの解除を引き継いでしまう。
    expect(nextRoutingId("bus-", info(applied), editing)).toBe("bus-2");
  });
});

describe("addOutput", () => {
  it("メインの出力とほかの出力に重ならない最初のチャンネルの組を選ぶ", () => {
    const config = {
      ...empty(),
      outputs: [{ entity_id: "out-1", name: "A", device_channel: 2 }],
    };
    expect(addOutput(config, { entity_id: "out-2", name: "B" }).outputs[1]?.device_channel).toBe(4);
  });
});

describe("removeNode", () => {
  it("ノードを端点とするセンドも取り除く", () => {
    const config: RoutingConfig = {
      ...empty(),
      buses: [{ entity_id: "bus-1", name: "A", kind: "USER" }],
      outputs: [{ entity_id: "out-1", name: "B", device_channel: 2 }],
      sends: [
        { entity_id: "send-1", from: "ch1", to: "bus-1", tap: "PRE_FADER" },
        { entity_id: "send-2", from: "master", to: "out-1", tap: "POST_FADER" },
      ],
    };
    expect(removeNode(config, "bus-1").sends.map((s) => s.entity_id)).toEqual(["send-2"]);
  });
});
