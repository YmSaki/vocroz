// SPDX-License-Identifier: AGPL-3.0-only
// フェイクエンジンの SRTP 外部出力。pa-engine の session（ExternalOutputBook と HostCommandHandler）を
// 簡略化して、設定、鍵、開始と停止、接続テストを再現する。
// 規則の正本は C++ 側（modules/session/src/external_output_book.cpp、
// docs/adr/0040-srtp-external-output.md）で、ここは画面の開発と手動の確認のための写しである。
// 鍵は形式を確かめて「設定済み」にするだけで、中身は持たない（エンジンも UI へ返さない）。

import type {
  ErrorCode,
  ExternalOutput,
  ExternalOutputConfig,
  ExternalOutputResult,
} from "../messages";

/** ペイロードタイプの範囲（動的な範囲）。control-protocol の定数と同じ値。 */
const kMinPayloadType = 96;
const kMaxPayloadType = 127;
/** ホスト名の最大長（DNS の名前の上限）。 */
const kMaxHostLength = 253;
/** SDES の鍵の長さ（マスターキー 16 バイト + マスターソルト 14 バイト）。 */
const kKeyBytes = 30;
/** 接続テストで試験音を送る長さ（ms）。pa-engine の kExternalOutputTestDuration と同じ値。 */
export const kFakeExternalOutputTestMs = 3000;
/**
 * 名前を解決できない宛先として扱う接尾辞。RFC 6761 で解決できないことが決まっている .invalid を使い、
 * 画面で RESOLVE_FAILED を再現できるようにする。
 */
const kUnresolvableSuffix = ".invalid";

/** 起動時の設定。宛先は未設定、送り出すのは Master。 */
function initialConfig(): ExternalOutputConfig {
  return {
    host: "",
    port: 5004,
    source_id: "master",
    profile: "OPUS_STEREO",
    payload_type: 111,
    crypto_suite: "AES_CM_128_HMAC_SHA1_80",
  };
}

/** SDES の inline: 形式の鍵として受け付けられるか。transport の parse_srtp_master_key() の写し。 */
export function isValidSrtpKey(text: string): boolean {
  const body = text.startsWith("inline:") ? text.slice("inline:".length) : text;
  if (!/^[A-Za-z0-9+/]+={0,2}$/.test(body)) return false;
  let bytes: string;
  try {
    bytes = atob(body);
  } catch {
    return false;
  }
  if (bytes.length !== kKeyBytes) return false;
  // すべて 0 の鍵は、未設定の初期値を送り出す誤りを防ぐため受け付けない。
  return [...bytes].some((c) => c !== "\0");
}

/** コマンドの適用の結果。 */
export type ExternalOutputChange = { ok: true; revision: number } | { ok: false; error: ErrorCode };

/** フェイクエンジンの外部出力。 */
export class FakeExternalOutput {
  private state: ExternalOutput;

  constructor(entityId: string) {
    this.state = {
      entity_id: entityId,
      revision: 0,
      config: initialConfig(),
      key_configured: false,
      // 起動時は停止から始める（初期ミュートの製品要件）。
      state: "STOPPED",
      last_result: null,
    };
  }

  /** 現在の状態。 */
  info(): ExternalOutput {
    return this.state;
  }

  /**
   * 設定を変える。送信中とテスト中は変えられない。
   * @param config 新しい設定
   * @param isSource source_id が送り出せるバスか Master か
   */
  setConfig(config: ExternalOutputConfig, isSource: boolean): ExternalOutputChange {
    if (!isSource) return { ok: false, error: "NOT_FOUND" };
    if (
      config.host.length > kMaxHostLength ||
      !Number.isInteger(config.port) ||
      config.port < 1 ||
      config.port > 65535 ||
      !Number.isInteger(config.payload_type) ||
      config.payload_type < kMinPayloadType ||
      config.payload_type > kMaxPayloadType
    ) {
      return { ok: false, error: "INVALID_STATE" };
    }
    if (this.state.state !== "STOPPED") return { ok: false, error: "INVALID_STATE" };
    if (JSON.stringify(config) !== JSON.stringify(this.state.config)) {
      // 宛先が変われば前の結果はもう当てはまらない。
      this.update({ config: { ...config }, last_result: null });
    }
    return { ok: true, revision: this.state.revision };
  }

  /** 鍵を設定する。形式が違う鍵は受け付けない。 */
  setKey(key: string): ExternalOutputChange {
    if (!isValidSrtpKey(key)) return { ok: false, error: "INVALID_STATE" };
    if (this.state.state !== "STOPPED") return { ok: false, error: "INVALID_STATE" };
    // 同じ鍵を貼り直しても revision を進める（UI からは同じかどうかを見分けられないため）。
    this.update({ key_configured: true, last_result: null });
    return { ok: true, revision: this.state.revision };
  }

  /**
   * 送信を始めるか止める。
   * @param running 始めるなら true
   * @param isSource 設定の source_id が今も送り出せるか
   * @return 適用の結果。開始に失敗したときも適用済み（last_result に結果を載せる）
   */
  setRunning(running: boolean, isSource: boolean): ExternalOutputChange {
    const current = this.state.state;
    if (!running) {
      if (current !== "STOPPED") this.update({ state: "STOPPED" });
      return { ok: true, revision: this.state.revision };
    }
    if (current === "RUNNING") return { ok: true, revision: this.state.revision };
    if (current === "TESTING" || !isSource || !this.ready()) {
      return { ok: false, error: "INVALID_STATE" };
    }
    const result = this.resolve();
    this.update({ state: result === "OK" ? "RUNNING" : "STOPPED", last_result: result });
    return { ok: true, revision: this.state.revision };
  }

  /** 接続テストを始める。止めている間だけ行える。 */
  startTest(): ExternalOutputChange {
    if (this.state.state !== "STOPPED" || !this.ready()) {
      return { ok: false, error: "INVALID_STATE" };
    }
    const result = this.resolve();
    this.update(
      result === "OK" ? { state: "TESTING", last_result: null } : { last_result: result },
    );
    return { ok: true, revision: this.state.revision };
  }

  /**
   * 接続テストを終える。結果はエンジンの観測なので revision を進めない。
   * @return テスト中だったか
   */
  finishTest(): boolean {
    if (this.state.state !== "TESTING") return false;
    this.state = { ...this.state, state: "STOPPED", last_result: "OK" };
    return true;
  }

  /**
   * 送り出していたバスがなくなったときに止める。
   * @return 送信かテストを止めたか
   */
  stopWithoutSource(): boolean {
    if (this.state.state === "STOPPED") return false;
    this.update({ state: "STOPPED" });
    return true;
  }

  /** 宛先と鍵がそろっているか。そろわなければ始めない（平文では送らない）。 */
  private ready(): boolean {
    return this.state.config.host !== "" && this.state.key_configured;
  }

  private resolve(): ExternalOutputResult {
    return this.state.config.host.endsWith(kUnresolvableSuffix) ? "RESOLVE_FAILED" : "OK";
  }

  private update(patch: Partial<ExternalOutput>): void {
    this.state = { ...this.state, ...patch, revision: this.state.revision + 1 };
  }
}
