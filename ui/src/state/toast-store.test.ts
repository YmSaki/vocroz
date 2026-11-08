// SPDX-License-Identifier: AGPL-3.0-only
import { describe, expect, it } from "vitest";
import { FakeTimers } from "../utils/fake-timers";
import { kInfoToastMs, kMaxVisibleToasts, kWarningToastMs, ToastStore } from "./toast-store";

describe("ToastStore", () => {
  it("同時に表示するのは最大 3 件で、あふれた古い通知は履歴にだけ残す", () => {
    const store = new ToastStore(new FakeTimers(), () => 0);
    for (let i = 1; i <= 5; i++) store.push("info", `n${i}`);
    expect(store.get().visible.map((t) => t.text)).toEqual(["n3", "n4", "n5"]);
    expect(store.get().visible).toHaveLength(kMaxVisibleToasts);
    expect(store.get().history.map((t) => t.text)).toEqual(["n5", "n4", "n3", "n2", "n1"]);
  });

  it("情報は 4 秒、警告は 8 秒で表示を外す", () => {
    const timers = new FakeTimers();
    const store = new ToastStore(timers, () => 0);
    store.push("info", "info");
    store.push("warning", "warning");
    timers.advance(kInfoToastMs);
    expect(store.get().visible.map((t) => t.text)).toEqual(["warning"]);
    timers.advance(kWarningToastMs - kInfoToastMs);
    expect(store.get().visible).toEqual([]);
    expect(store.get().history).toHaveLength(2);
  });

  it("閉じた通知のタイマーを残さない", () => {
    const timers = new FakeTimers();
    const store = new ToastStore(timers, () => 0);
    const id = store.push("info", "x");
    store.dismiss(id);
    expect(timers.pendingCount()).toBe(0);
  });
});
