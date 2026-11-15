// SPDX-License-Identifier: AGPL-3.0-only
// プラグインの失敗の通知。エンジンは失敗を状態として送るので、新しく失敗したものだけをトーストにする。
import { describe, expect, it } from "vitest";
import type { PluginsInfo } from "../../ipc/messages";
import { PluginStore } from "../../state/plugin-store";
import { ToastStore } from "../../state/toast-store";
import { FakeTimers } from "../../utils/fake-timers";
import { connectPluginNotices } from "./feedback";

function info(patch: Partial<PluginsInfo>): PluginsInfo {
  return {
    entity_id: "plugins",
    revision: 1,
    scanning: false,
    catalog_generation: 1,
    catalog_size: 0,
    blocked: [],
    inserts: [],
    ...patch,
  };
}

describe("connectPluginNotices", () => {
  it("新しく失敗した挿入と、新しく外したファイルだけを知らせ、最初の状態では知らせない", () => {
    const plugins = new PluginStore();
    const toasts = new ToastStore(new FakeTimers(), () => 0);
    connectPluginNotices(plugins, toasts, (id) => `名前-${id}`);
    const failed = { entity_id: "fx-1", state: "FAILED" as const, failure: "NOT_FOUND" as const };

    plugins.apply({ type: "plugins_updated", plugins: info({ inserts: [failed] }) });
    expect(toasts.get().history).toHaveLength(0);

    const ready = { entity_id: "fx-2", state: "READY" as const, failure: null };
    plugins.apply({ type: "plugins_updated", plugins: info({ inserts: [failed, ready] }) });
    expect(toasts.get().history).toHaveLength(0);

    const crashed = { ...ready, state: "FAILED" as const, failure: "PROCESSING_FAILED" as const };
    plugins.apply({ type: "plugins_updated", plugins: info({ inserts: [failed, crashed] }) });
    expect(toasts.get().history.map((t) => t.text)).toEqual([
      "名前-fx-2 が止まりました。その経路は無音にしています。読み込み直すまで音は戻りません。",
    ]);

    plugins.apply({
      type: "plugins_updated",
      plugins: info({
        inserts: [failed, crashed],
        blocked: [{ path: "a.vst3", reason: "CRASHED" }],
      }),
    });
    expect(toasts.get().history[0]?.text).toBe(
      "1 個のプラグインファイルを、スキャンで問題があったため外しました。",
    );
  });
});
