// SPDX-License-Identifier: AGPL-3.0-only
import { describe, expect, it } from "vitest";
import { OptimisticStore } from "./optimistic-store";

describe("OptimisticStore", () => {
  it("操作中は ACK がそろっても値を外さず、操作の終了で外す", () => {
    const store = new OptimisticStore();
    store.begin("ch1", -3);
    store.settle("ch1");
    expect(store.get().get("ch1")?.value).toBe(-3);
    store.release("ch1");
    expect(store.get().has("ch1")).toBe(false);
  });

  it("操作の終了後は ACK がそろった時点で外す", () => {
    const store = new OptimisticStore();
    store.begin("ch1", -3);
    store.release("ch1");
    expect(store.get().get("ch1")?.value).toBe(-3);
    store.settle("ch1");
    expect(store.get().has("ch1")).toBe(false);
  });

  it("新しい操作は前の ACK のそろいを引き継がない", () => {
    const store = new OptimisticStore();
    store.begin("ch1", -3);
    store.settle("ch1");
    store.begin("ch1", -4);
    store.release("ch1");
    expect(store.get().get("ch1")?.value).toBe(-4);
  });

  it("期限切れでは操作中でも値を外す", () => {
    const store = new OptimisticStore();
    store.begin("ch1", -3);
    store.revert("ch1");
    expect(store.get().has("ch1")).toBe(false);
  });
});
