// SPDX-License-Identifier: AGPL-3.0-only
import { describe, expect, it } from "vitest";
import type { ChannelStrip } from "../../ipc/messages";
import { stripStatus } from "./strip-status";

const base: ChannelStrip = {
  entity_id: "ch1",
  revision: 1,
  participant_id: "p1",
  name: "Vo",
  short_id: "#01",
  connection_state: "CONNECTED",
  signal_state: "RECEIVING",
  send_muted: false,
  channel_muted: false,
  gain_db: 0,
  error_code: null,
  source_kind: "MICROPHONE",
  channel_count: 1,
  source_number: 1,
  return_muted: true,
};

describe("stripStatus", () => {
  it("接続状態のラベルを信号状態より優先する", () => {
    expect(stripStatus({ ...base, connection_state: "FAILED", signal_state: null }).key).toBe(
      "connection.FAILED",
    );
    expect(stripStatus({ ...base, connection_state: "LOST", signal_state: null }).key).toBe(
      "connection.LOST",
    );
    expect(stripStatus({ ...base, connection_state: "RECONNECTING", signal_state: null }).key).toBe(
      "connection.RECONNECTING",
    );
    expect(stripStatus({ ...base, connection_state: "CONNECTING", signal_state: null }).key).toBe(
      "connection.CONNECTING",
    );
  });

  it("NO_SIGNAL だけを警告として出し、受信中、一時停止、再開待ちはラベルを出さない", () => {
    expect(stripStatus({ ...base, signal_state: "NO_SIGNAL" })).toEqual({
      key: "signal.NO_SIGNAL",
      warning: true,
    });
    for (const signal of ["RECEIVING", "PAUSED", "RESUMING"] as const) {
      expect(stripStatus({ ...base, signal_state: signal })).toEqual({ key: null, warning: false });
    }
  });

  it("送信ミュート中（PAUSED）を NO_SIGNAL と表示しない", () => {
    expect(stripStatus({ ...base, send_muted: true, signal_state: "PAUSED" }).key).toBeNull();
  });
});
