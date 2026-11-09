// SPDX-License-Identifier: AGPL-3.0-only
import { describe, expect, it } from "vitest";
import type { RoutingConfig } from "../messages";
import { checkRouting, emptyRoutingConfig, FakeRouting } from "./fake-routing";

/** ch1 からキューバスへプリフェーダーで送り、追加の出力から出す構成。 */
function cueConfig(): RoutingConfig {
  return {
    ...emptyRoutingConfig(),
    buses: [{ entity_id: "bus-cue", name: "返し", kind: "CUE" }],
    outputs: [{ entity_id: "out-mon", name: "モニター", device_channel: 2 }],
    sends: [
      { entity_id: "send-1", from: "ch1", to: "bus-cue", tap: "PRE_FADER" },
      { entity_id: "send-2", from: "bus-cue", to: "out-mon", tap: "POST_FADER" },
    ],
  };
}

/** Master から遅延ノードとバスを経て Master へ戻るループ。 */
function echoConfig(): RoutingConfig {
  return {
    ...emptyRoutingConfig(),
    buses: [{ entity_id: "bus-fx", name: "FX", kind: "USER" }],
    delays: [{ entity_id: "delay-echo", delay_ms: 250 }],
    sends: [
      { entity_id: "send-1", from: "master", to: "delay-echo", tap: "POST_FADER" },
      { entity_id: "send-2", from: "delay-echo", to: "bus-fx", tap: "POST_FADER" },
      { entity_id: "send-3", from: "bus-fx", to: "master", tap: "POST_FADER" },
    ],
  };
}

describe("checkRouting", () => {
  it("問題のない構成を受け付ける", () => {
    expect(checkRouting(cueConfig(), ["ch1"])).toEqual({
      issue: null,
      issue_entity_id: null,
      looped_entity_ids: [],
      needs_approval: false,
    });
  });

  it("消えたストリップへのセンドを UNKNOWN_NODE にする", () => {
    expect(checkRouting(cueConfig(), [])).toMatchObject({
      issue: "UNKNOWN_NODE",
      issue_entity_id: "send-1",
    });
  });

  it("接頭辞の違う ID と重複した ID を拒否する", () => {
    const config = cueConfig();
    config.buses.push({ entity_id: "fx", name: "", kind: "USER" });
    expect(checkRouting(config, ["ch1"]).issue).toBe("INVALID_ID");
    const duplicated = cueConfig();
    duplicated.buses.push({ entity_id: "bus-cue", name: "", kind: "USER" });
    expect(checkRouting(duplicated, ["ch1"]).issue).toBe("DUPLICATE_ID");
  });

  it("メインの出力と重なる出力と、範囲外のチャンネルを拒否する", () => {
    const config = cueConfig();
    config.outputs[0] = { entity_id: "out-mon", name: "", device_channel: 1 };
    expect(checkRouting(config, ["ch1"]).issue).toBe("OVERLAPPING_OUTPUTS");
    config.outputs[0] = { entity_id: "out-mon", name: "", device_channel: 63 };
    expect(checkRouting(config, ["ch1"]).issue).toBe("OUTPUT_CHANNEL_OUT_OF_RANGE");
  });

  it("Master からのプリフェーダーと、ストリップから Master への重複した結線を拒否する", () => {
    const config = echoConfig();
    config.sends[0] = { ...config.sends[0]!, tap: "PRE_FADER" };
    expect(checkRouting(config, []).issue).toBe("INVALID_SEND_TAP");
    const implicit = cueConfig();
    implicit.sends.push({ entity_id: "send-3", from: "ch1", to: "master", tap: "POST_FADER" });
    expect(checkRouting(implicit, ["ch1"]).issue).toBe("INVALID_SEND");
  });

  it("遅延ノードを通るループは承認を求め、遅延のないループは拒否する", () => {
    const delayed = checkRouting(echoConfig(), []);
    expect(delayed.issue).toBeNull();
    expect(delayed.needs_approval).toBe(true);
    expect(delayed.looped_entity_ids).toEqual(["bus-fx", "delay-echo", "master"]);

    const undelayed: RoutingConfig = {
      ...emptyRoutingConfig(),
      buses: [{ entity_id: "bus-fx", name: "FX", kind: "USER" }],
      sends: [
        { entity_id: "send-1", from: "master", to: "bus-fx", tap: "POST_FADER" },
        { entity_id: "send-2", from: "bus-fx", to: "master", tap: "POST_FADER" },
      ],
    };
    expect(checkRouting(undelayed, [])).toMatchObject({
      issue: "UNDELAYED_LOOP",
      looped_entity_ids: ["bus-fx", "master"],
    });
  });

  it("ブロック長より短い遅延ノードを拒否する", () => {
    const config = echoConfig();
    config.delays[0] = { entity_id: "delay-echo", delay_ms: 1 };
    expect(checkRouting(config, []).issue).toBe("DELAY_OUT_OF_RANGE");
  });
});

describe("FakeRouting", () => {
  it("ループは承認したときだけ適用し、新しいバスとセンドはミュートで作る", () => {
    const routing = new FakeRouting("routing");
    routing.stage(echoConfig(), []);
    expect(routing.commit(false, [])).toEqual({ ok: false, error: "INVALID_STATE" });
    expect(routing.commit(true, []).ok).toBe(true);
    const info = routing.info();
    expect(info.applied).toEqual(echoConfig());
    expect(info.feedback_approved).toBe(true);
    expect(info.staged).toBeNull();
    expect(info.levels.every((l) => l.muted && l.gain_db === 0)).toBe(true);
  });

  it("適用し直しても残るバスとセンドの値を引き継ぐ", () => {
    const routing = new FakeRouting("routing");
    routing.stage(cueConfig(), ["ch1"]);
    routing.commit(false, ["ch1"]);
    routing.setMute("bus-cue", false);
    routing.setGain("bus-cue", 20);
    routing.stage(cueConfig(), ["ch1"]);
    routing.commit(false, ["ch1"]);
    expect(routing.level("bus-cue")).toMatchObject({ muted: false, gain_db: 10 });
  });

  it("ストリップの削除で、そのストリップを参照するセンドを取り除く", () => {
    const routing = new FakeRouting("routing");
    routing.stage(cueConfig(), ["ch1"]);
    routing.commit(false, ["ch1"]);
    expect(routing.forgetStrip("ch1", [])).toBe(true);
    expect(routing.info().applied.sends.map((s) => s.entity_id)).toEqual(["send-2"]);
    expect(routing.level("send-1")).toBeUndefined();
  });
});
