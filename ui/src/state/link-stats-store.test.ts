// SPDX-License-Identifier: AGPL-3.0-only
// 受信の統計値のストアのテスト。

import { describe, expect, it } from "vitest";
import { LinkStatsStore } from "./link-stats-store";

describe("LinkStatsStore", () => {
  it("届いた一覧で置き換え、一覧にないチャンネルの値を残さない", () => {
    const store = new LinkStatsStore();
    store.apply({ type: "link_stats", stats: { ch1: { jitter_ms: 2, buffer_ms: 12 } } });
    store.apply({ type: "link_stats", stats: { ch2: { jitter_ms: null, buffer_ms: null } } });
    expect(store.get()).toEqual({ ch2: { jitter_ms: null, buffer_ms: null } });
  });
});
