// SPDX-License-Identifier: AGPL-3.0-only
import { describe, expect, it } from "vitest";
import type { ExternalOutputConfig } from "../messages";
import { FakeExternalOutput, isValidSrtpKey } from "./fake-external-output";

const kKey = "inline:AQIDBAUGBwgJCgsMDQ4PEBESExQVFhcYGRobHB0e";

function config(overrides: Partial<ExternalOutputConfig> = {}): ExternalOutputConfig {
  return {
    host: "192.0.2.10",
    port: 5004,
    source_id: "master",
    profile: "OPUS_STEREO",
    payload_type: 111,
    crypto_suite: "AES_CM_128_HMAC_SHA1_80",
    ...overrides,
  };
}

describe("FakeExternalOutput", () => {
  it("停止し、宛先も鍵もない状態で始める", () => {
    const output = new FakeExternalOutput("external_output").info();
    expect(output).toMatchObject({ state: "STOPPED", key_configured: false, last_result: null });
    expect(output.config.host).toBe("");
  });

  it("宛先か鍵がなければ送信も接続テストも始めない（平文では送らない）", () => {
    const output = new FakeExternalOutput("external_output");
    expect(output.setRunning(true, true)).toEqual({ ok: false, error: "INVALID_STATE" });
    output.setConfig(config(), true);
    expect(output.startTest()).toEqual({ ok: false, error: "INVALID_STATE" });
    output.setKey(kKey);
    expect(output.setRunning(true, true).ok).toBe(true);
    expect(output.info()).toMatchObject({ state: "RUNNING", last_result: "OK" });
  });

  it("送信中は設定も鍵も変えられない", () => {
    const output = new FakeExternalOutput("external_output");
    output.setConfig(config(), true);
    output.setKey(kKey);
    output.setRunning(true, true);
    expect(output.setConfig(config({ port: 6000 }), true).ok).toBe(false);
    expect(output.setKey(kKey).ok).toBe(false);
  });

  it("接続テストの終わりは観測なので revision を進めない", () => {
    const output = new FakeExternalOutput("external_output");
    output.setConfig(config(), true);
    output.setKey(kKey);
    output.startTest();
    const revision = output.info().revision;
    expect(output.info().state).toBe("TESTING");
    expect(output.finishTest()).toBe(true);
    expect(output.info()).toMatchObject({ state: "STOPPED", last_result: "OK", revision });
  });

  it("名前を解決できない宛先では開始を失敗として記録する", () => {
    const output = new FakeExternalOutput("external_output");
    output.setConfig(config({ host: "receiver.invalid" }), true);
    output.setKey(kKey);
    expect(output.setRunning(true, true).ok).toBe(true);
    expect(output.info()).toMatchObject({ state: "STOPPED", last_result: "RESOLVE_FAILED" });
  });

  it("ない送り出し元とペイロードタイプの範囲外を拒否する", () => {
    const output = new FakeExternalOutput("external_output");
    expect(output.setConfig(config(), false)).toEqual({ ok: false, error: "NOT_FOUND" });
    expect(output.setConfig(config({ payload_type: 95 }), true).ok).toBe(false);
    expect(output.setConfig(config({ payload_type: 128 }), true).ok).toBe(false);
  });
});

describe("isValidSrtpKey", () => {
  it("inline: の有無を問わず 30 バイトの Base64 を受け付ける", () => {
    expect(isValidSrtpKey(kKey)).toBe(true);
    expect(isValidSrtpKey(kKey.slice("inline:".length))).toBe(true);
  });

  it("長さの違う鍵、Base64 でない鍵、すべて 0 の鍵を受け付けない", () => {
    expect(isValidSrtpKey("inline:AQIDBA==")).toBe(false);
    expect(isValidSrtpKey("inline:not base64!")).toBe(false);
    expect(isValidSrtpKey(`inline:${btoa("\0".repeat(30))}`)).toBe(false);
    expect(isValidSrtpKey(`${kKey}|2^31`)).toBe(false);
  });
});
