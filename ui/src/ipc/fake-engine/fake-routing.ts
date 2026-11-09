// SPDX-License-Identifier: AGPL-3.0-only
// フェイクエンジンのルーティング。pa-engine の session（RoutingBook と check_routing）を簡略化して、
// ステージング、検証、承認、適用、バスとセンドのレベルを再現する。
// 規則の正本は C++ 側（modules/session/src/routing_validator.cpp、docs/adr/0026-routing-staging.md）で、
// ここは画面の開発と手動の確認のための写しである。件数の上限など、画面で再現する必要のない規則は省く。

import type {
  ErrorCode,
  GainDb,
  RouteLevel,
  RoutingCheck,
  RoutingConfig,
  RoutingInfo,
  RoutingIssue,
} from "../messages";

/** エンジンのサンプリング周波数（Hz）。遅延の ms をサンプル数に直すのに使う。 */
const kSampleRate = 48000;
/** 遅延ノードの下限（サンプル）。ループの戻りを次のブロックで読むため、ブロック長（128）より短くできない。 */
const kMinFeedbackDelaySamples = 128;
/** 遅延ノードの上限（サンプル、2 秒）。 */
const kMaxFeedbackDelaySamples = 2 * kSampleRate;
/** 手動ディレイの上限（サンプル、1 秒）。 */
const kMaxInputDelaySamples = kSampleRate;
/** 出力デバイスのチャンネル数の上限。出力はステレオなので、先頭のチャンネルは 62 まで。 */
const kMaxOutputChannels = 64;
/** フェーダーの上限（dB）。チャンネルと同じ範囲にする。 */
const kMaxFaderDb = 10;

const kMasterId = "master";

type Role = "strip" | "master" | "bus" | "output" | "delay";

/** 検証で見つかった問題。 */
interface Problem {
  issue: RoutingIssue;
  entityId: string | null;
}

function delaySamples(delayMs: number): number {
  return Math.round((delayMs * kSampleRate) / 1000);
}

/** 空の構成。 */
export function emptyRoutingConfig(): RoutingConfig {
  return { buses: [], outputs: [], sends: [], delays: [], input_delays: [] };
}

function registerIds(
  config: RoutingConfig,
  stripIds: readonly string[],
  nodes: Map<string, Role>,
): Problem | null {
  for (const id of stripIds) nodes.set(id, "strip");
  nodes.set(kMasterId, "master");
  const sendIds = new Set<string>();
  const add = (id: string, prefix: string, role: Role | null): Problem | null => {
    if (!id.startsWith(prefix) || id.length === prefix.length) {
      return { issue: "INVALID_ID", entityId: id };
    }
    if (nodes.has(id) || sendIds.has(id)) return { issue: "DUPLICATE_ID", entityId: id };
    if (role === null) sendIds.add(id);
    else nodes.set(id, role);
    return null;
  };
  const items: [string, string, Role | null][] = [
    ...config.buses.map((b): [string, string, Role] => [b.entity_id, "bus-", "bus"]),
    ...config.outputs.map((o): [string, string, Role] => [o.entity_id, "out-", "output"]),
    ...config.delays.map((d): [string, string, Role] => [d.entity_id, "delay-", "delay"]),
    ...config.sends.map((s): [string, string, null] => [s.entity_id, "send-", null]),
  ];
  for (const [id, prefix, role] of items) {
    const problem = add(id, prefix, role);
    if (problem !== null) return problem;
  }
  return null;
}

function checkOutputs(config: RoutingConfig): Problem | null {
  // メインの出力はチャンネル 0〜1 を使うので、追加の出力はそこと重ねられない。
  const used = [0];
  for (const output of config.outputs) {
    const first = output.device_channel;
    if (!Number.isInteger(first) || first < 0 || first > kMaxOutputChannels - 2) {
      return { issue: "OUTPUT_CHANNEL_OUT_OF_RANGE", entityId: output.entity_id };
    }
    if (used.some((u) => Math.abs(u - first) < 2)) {
      return { issue: "OVERLAPPING_OUTPUTS", entityId: output.entity_id };
    }
    used.push(first);
  }
  return null;
}

function checkDelays(config: RoutingConfig, nodes: Map<string, Role>): Problem | null {
  for (const delay of config.delays) {
    const samples = delaySamples(delay.delay_ms);
    if (samples < kMinFeedbackDelaySamples || samples > kMaxFeedbackDelaySamples) {
      return { issue: "DELAY_OUT_OF_RANGE", entityId: delay.entity_id };
    }
  }
  const delayed = new Set<string>();
  for (const delay of config.input_delays) {
    if (nodes.get(delay.strip_id) !== "strip") {
      return { issue: "UNKNOWN_NODE", entityId: delay.strip_id };
    }
    if (delayed.has(delay.strip_id)) return { issue: "DUPLICATE_ID", entityId: delay.strip_id };
    delayed.add(delay.strip_id);
    if (delay.delay_ms < 0 || delaySamples(delay.delay_ms) > kMaxInputDelaySamples) {
      return { issue: "DELAY_OUT_OF_RANGE", entityId: delay.strip_id };
    }
  }
  return null;
}

function checkSends(config: RoutingConfig, nodes: Map<string, Role>): Problem | null {
  const pairs = new Set<string>();
  for (const send of config.sends) {
    const from = nodes.get(send.from);
    const to = nodes.get(send.to);
    if (from === undefined || to === undefined) {
      return { issue: "UNKNOWN_NODE", entityId: send.entity_id };
    }
    // 出力からは送れず、ストリップへは送れない。ストリップから Master は暗黙の結線と重なる。
    const pair = `${send.from}\u0000${send.to}`;
    if (
      from === "output" ||
      to === "strip" ||
      send.from === send.to ||
      (from === "strip" && to === "master") ||
      pairs.has(pair)
    ) {
      return { issue: "INVALID_SEND", entityId: send.entity_id };
    }
    pairs.add(pair);
    if (send.tap === "PRE_FADER" && from !== "strip" && from !== "bus") {
      return { issue: "INVALID_SEND_TAP", entityId: send.entity_id };
    }
  }
  return null;
}

/** 有向グラフの強連結成分のうち、ループをなすもの（2 ノード以上）のノードを返す。 */
function loopedNodes(nodes: readonly string[], edges: ReadonlyMap<string, string[]>): string[] {
  // Tarjan の算法。ノード数は最大でも数十なので、再帰で十分。
  let counter = 0;
  const index = new Map<string, number>();
  const low = new Map<string, number>();
  const stack: string[] = [];
  const onStack = new Set<string>();
  const looped: string[] = [];
  const visit = (v: string) => {
    index.set(v, counter);
    low.set(v, counter);
    counter += 1;
    stack.push(v);
    onStack.add(v);
    for (const w of edges.get(v) ?? []) {
      if (!index.has(w)) {
        visit(w);
        low.set(v, Math.min(low.get(v) ?? 0, low.get(w) ?? 0));
      } else if (onStack.has(w)) {
        low.set(v, Math.min(low.get(v) ?? 0, index.get(w) ?? 0));
      }
    }
    if (low.get(v) === index.get(v)) {
      const component: string[] = [];
      let w: string | undefined;
      do {
        w = stack.pop();
        if (w === undefined) break;
        onStack.delete(w);
        component.push(w);
      } while (w !== v);
      if (component.length > 1) looped.push(...component);
    }
  };
  for (const v of nodes) if (!index.has(v)) visit(v);
  return looped.sort();
}

function buildEdges(
  config: RoutingConfig,
  nodes: Map<string, Role>,
  skip: (id: string) => boolean,
): Map<string, string[]> {
  const edges = new Map<string, string[]>();
  const connect = (from: string, to: string) => {
    if (skip(from) || skip(to)) return;
    edges.set(from, [...(edges.get(from) ?? []), to]);
  };
  for (const [id, role] of nodes) if (role === "strip") connect(id, kMasterId);
  for (const send of config.sends) connect(send.from, send.to);
  return edges;
}

/**
 * 構成を検証する。「ID → 出力のチャンネル → 遅延 → センド → ループ」の順に確かめ、最初の問題を返す。
 * @param config 構成
 * @param stripIds 今あるチャンネルストリップの entity_id
 */
export function checkRouting(config: RoutingConfig, stripIds: readonly string[]): RoutingCheck {
  const nodes = new Map<string, Role>();
  const problem =
    registerIds(config, stripIds, nodes) ??
    checkOutputs(config) ??
    checkDelays(config, nodes) ??
    checkSends(config, nodes);
  if (problem !== null) {
    return {
      issue: problem.issue,
      issue_entity_id: problem.entityId,
      looped_entity_ids: [],
      needs_approval: false,
    };
  }
  const ids = [...nodes.keys()];
  // 遅延ノードを除いても残るループは、遅延のないループ（承認しても適用できない）。
  const undelayed = loopedNodes(
    ids,
    buildEdges(config, nodes, (id) => nodes.get(id) === "delay"),
  );
  if (undelayed.length > 0) {
    return {
      issue: "UNDELAYED_LOOP",
      issue_entity_id: undelayed[0] ?? null,
      looped_entity_ids: undelayed,
      needs_approval: false,
    };
  }
  const looped = loopedNodes(
    ids,
    buildEdges(config, nodes, () => false),
  );
  return {
    issue: null,
    issue_entity_id: null,
    looped_entity_ids: looped,
    needs_approval: looped.length > 0,
  };
}

/** バスとセンドのレベル。残る entity_id は値を引き継ぎ、新しいものはミュートで 0 dB（初期ミュート）。 */
function levelsFor(config: RoutingConfig, previous: readonly RouteLevel[]): RouteLevel[] {
  return [...config.buses, ...config.sends].map(
    ({ entity_id }) =>
      previous.find((l) => l.entity_id === entity_id) ?? {
        entity_id,
        revision: 1,
        gain_db: 0,
        muted: true,
      },
  );
}

/** コマンドの結果。成功なら適用後の revision、失敗ならエラーコード。 */
export type RoutingResult = { ok: true; revision: number } | { ok: false; error: ErrorCode };

/** フェイクエンジンのルーティングの状態。 */
export class FakeRouting {
  private state: RoutingInfo;

  constructor(entityId: string) {
    this.state = {
      entity_id: entityId,
      revision: 1,
      applied: emptyRoutingConfig(),
      feedback_approved: false,
      staged: null,
      levels: [],
    };
  }

  /** ルーティング全体の状態。 */
  info(): RoutingInfo {
    return this.state;
  }

  /** バスかセンドのレベル。 */
  level(entityId: string): RouteLevel | undefined {
    return this.state.levels.find((l) => l.entity_id === entityId);
  }

  /** 構成をステージングする。問題があっても受け付け、理由を検証の結果に残す。 */
  stage(config: RoutingConfig, stripIds: readonly string[]): number {
    this.state = {
      ...this.state,
      staged: { config, check: checkRouting(config, stripIds) },
      revision: this.state.revision + 1,
    };
    return this.state.revision;
  }

  /** ステージング中の構成を検証し直して適用する。 */
  commit(approveFeedback: boolean, stripIds: readonly string[]): RoutingResult {
    const staged = this.state.staged;
    if (staged === null) return { ok: false, error: "INVALID_STATE" };
    // ステージングのあとでストリップが消えていることがあるので、適用の直前に検証し直す。
    const check = checkRouting(staged.config, stripIds);
    if (check.issue !== null || (check.needs_approval && !approveFeedback)) {
      this.state = { ...this.state, staged: { ...staged, check } };
      return { ok: false, error: "INVALID_STATE" };
    }
    this.state = {
      ...this.state,
      applied: staged.config,
      feedback_approved: check.needs_approval,
      staged: null,
      levels: levelsFor(staged.config, this.state.levels),
      revision: this.state.revision + 1,
    };
    return { ok: true, revision: this.state.revision };
  }

  /** ステージング中の構成を捨てる。 */
  discard(): RoutingResult {
    if (this.state.staged === null) return { ok: false, error: "INVALID_STATE" };
    this.state = { ...this.state, staged: null, revision: this.state.revision + 1 };
    return { ok: true, revision: this.state.revision };
  }

  /** バスかセンドのミュートを変える。値が変わったときだけ revision を進める。 */
  setMute(entityId: string, muted: boolean): RouteLevel | undefined {
    return this.updateLevel(entityId, (l) =>
      l.muted === muted ? l : { ...l, muted, revision: l.revision + 1 },
    );
  }

  /** バスかセンドのフェーダーを変える。連続値なので revision は進めない。 */
  setGain(entityId: string, gainDb: GainDb): RouteLevel | undefined {
    const clamped = gainDb === null ? null : Math.min(kMaxFaderDb, gainDb);
    return this.updateLevel(entityId, (l) => ({ ...l, gain_db: clamped }));
  }

  /**
   * ストリップの削除を反映する。適用中の構成からそのストリップを参照するセンドと手動ディレイを取り除く。
   * @returns 状態が変わったら true（UI に知らせる）
   */
  forgetStrip(stripId: string, stripIds: readonly string[]): boolean {
    const { applied } = this.state;
    const sends = applied.sends.filter((s) => s.from !== stripId && s.to !== stripId);
    const inputDelays = applied.input_delays.filter((d) => d.strip_id !== stripId);
    const changed =
      sends.length !== applied.sends.length || inputDelays.length !== applied.input_delays.length;
    const nextApplied = changed ? { ...applied, sends, input_delays: inputDelays } : applied;
    const staged =
      this.state.staged === null
        ? null
        : { ...this.state.staged, check: checkRouting(this.state.staged.config, stripIds) };
    this.state = {
      ...this.state,
      applied: nextApplied,
      levels: changed ? levelsFor(nextApplied, this.state.levels) : this.state.levels,
      staged,
      revision: this.state.revision + (changed ? 1 : 0),
    };
    return changed || staged !== null;
  }

  private updateLevel(
    entityId: string,
    update: (level: RouteLevel) => RouteLevel,
  ): RouteLevel | undefined {
    let result: RouteLevel | undefined;
    const levels = this.state.levels.map((l) => {
      if (l.entity_id !== entityId) return l;
      result = update(l);
      return result;
    });
    this.state = { ...this.state, levels };
    return result;
  }
}
