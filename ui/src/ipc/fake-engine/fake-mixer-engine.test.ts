// SPDX-License-Identifier: AGPL-3.0-only
import { describe, expect, it } from "vitest";
import { FakeTimers } from "../../utils/fake-timers";
import type { EngineToUiMessage } from "../messages";
import { FakeMixerEngine } from "./fake-mixer-engine";

describe("FakeMixerEngine", () => {
  it("すべてのチャンネルと Master をミュートした状態で始める", () => {
    const engine = new FakeMixerEngine({ meterIntervalMs: 0 });
    const { channels, master } = engine.snapshot();
    expect(channels.every((c) => c.channel_muted)).toBe(true);
    expect(master.master_muted).toBe(true);
  });

  it("revision が古い離散コマンドを REVISION_CONFLICT で拒否する", () => {
    const timers = new FakeTimers();
    const engine = new FakeMixerEngine({ timers, meterIntervalMs: 0 });
    const received: EngineToUiMessage[] = [];
    engine.subscribe((m) => received.push(m));
    engine.send({
      type: "set_channel_mute",
      request_id: "r1",
      entity_id: "ch1",
      expected_revision: 99,
      muted: false,
    });
    timers.advance(100);
    expect(received).toContainEqual({
      type: "reject",
      request_id: "r1",
      error_code: "REVISION_CONFLICT",
    });
  });

  it("出力デバイスの消失では、別のデバイスに切り替えず Master を安全ミュートにする", () => {
    const engine = new FakeMixerEngine({ meterIntervalMs: 0 });
    engine.simulateOutputLost();
    const { session, master } = engine.snapshot();
    expect(session.output_device_state).toBe("LOST");
    expect(session.output_device_name).toBeNull();
    expect(master).toMatchObject({ master_muted: true, master_mute_reason: "SAFETY" });
  });
});
