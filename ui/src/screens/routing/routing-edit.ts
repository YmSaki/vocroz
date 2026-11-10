// SPDX-License-Identifier: AGPL-3.0-only
// ルーティング画面の編集操作。画面に出している構成から、1 つの操作を加えた新しい構成を作る純粋関数。
// 作った構成はそのままエンジンへステージングし、検証と表示はエンジンから戻った状態で行う（エンジン主権）。
// 構成が正しいかはここでは判断しない。問題のある構成もステージングして、エンジンの検証の結果で示す。

import type {
  BusKind,
  RoutingBus,
  RoutingConfig,
  RoutingInfo,
  RoutingOutput,
  RoutingSend,
} from "../../ipc/messages";

/** 種類ごとの entity_id の接頭辞。エンジンの検証と同じ値（docs/adr/0026-routing-staging.md）。 */
export const kRoutingIdPrefix = {
  bus: "bus-",
  output: "out-",
  send: "send-",
  delay: "delay-",
} as const;

/** 新しい遅延ノードの遅延（ms）。フィードバックの戻りとして聞き分けやすい長さにする。 */
export const kDefaultDelayMs = 250;

/** 追加の出力の先頭チャンネルの上限。出力デバイスの 64 チャンネルのうち、ステレオの組の先頭は 62 まで。 */
export const kMaxOutputFirstChannel = 62;

/** メインの出力（チャンネル 0〜1）の次の、追加の出力が使える最初のチャンネル。 */
const kFirstExtraChannel = 2;

/** 画面で編集する構成。ステージング中の構成があればそれを、なければ適用中の構成を出す。 */
export function editingConfig(routing: RoutingInfo): RoutingConfig {
  return routing.staged?.config ?? routing.applied;
}

/**
 * まだ使われていない entity_id を作る。
 * 適用中の構成とレベルにある ID も避ける。消したバスと同じ ID で新しいバスを作ると、エンジンが前のバスの
 * フェーダーとミュートを引き継いでしまい、新しいバスがミュートで始まらなくなるため。
 * @param prefix 種類の接頭辞
 * @param routing ルーティング全体の状態
 * @param config 編集中の構成
 */
export function nextRoutingId(prefix: string, routing: RoutingInfo, config: RoutingConfig): string {
  const used = new Set<string>([
    ...allIds(routing.applied),
    ...allIds(config),
    ...routing.levels.map((l) => l.entity_id),
  ]);
  for (let n = 1; ; n += 1) {
    const id = `${prefix}${n}`;
    if (!used.has(id)) return id;
  }
}

function allIds(config: RoutingConfig): string[] {
  return [
    ...config.buses.map((b) => b.entity_id),
    ...config.outputs.map((o) => o.entity_id),
    ...config.sends.map((s) => s.entity_id),
    ...config.delays.map((d) => d.entity_id),
  ];
}

/** バスを足す。 */
export function addBus(config: RoutingConfig, bus: RoutingBus): RoutingConfig {
  return { ...config, buses: [...config.buses, bus] };
}

/** 追加の出力を足す。チャンネルは、メインの出力とほかの出力に重ならない最初の組にする。 */
export function addOutput(
  config: RoutingConfig,
  output: Omit<RoutingOutput, "device_channel">,
): RoutingConfig {
  const used = new Set(config.outputs.flatMap((o) => [o.device_channel, o.device_channel + 1]));
  let channel = kFirstExtraChannel;
  while (channel < kMaxOutputFirstChannel && (used.has(channel) || used.has(channel + 1))) {
    channel += 2;
  }
  return { ...config, outputs: [...config.outputs, { ...output, device_channel: channel }] };
}

/** 遅延ノードを足す。 */
export function addDelay(config: RoutingConfig, entityId: string): RoutingConfig {
  return {
    ...config,
    delays: [...config.delays, { entity_id: entityId, delay_ms: kDefaultDelayMs }],
  };
}

/** センドを足す。 */
export function addSend(config: RoutingConfig, send: RoutingSend): RoutingConfig {
  return { ...config, sends: [...config.sends, send] };
}

/** 手動ディレイを足す。 */
export function addInputDelay(config: RoutingConfig, stripId: string): RoutingConfig {
  return { ...config, input_delays: [...config.input_delays, { strip_id: stripId, delay_ms: 0 }] };
}

/**
 * バス、出力、遅延ノードを取り除く。そのノードを端点とするセンドも一緒に取り除く。
 * 端点のないセンドを残しても適用できないので、操作 1 回で適用できる形に戻せるようにする。
 */
export function removeNode(config: RoutingConfig, entityId: string): RoutingConfig {
  return {
    ...config,
    buses: config.buses.filter((b) => b.entity_id !== entityId),
    outputs: config.outputs.filter((o) => o.entity_id !== entityId),
    delays: config.delays.filter((d) => d.entity_id !== entityId),
    sends: config.sends.filter((s) => s.from !== entityId && s.to !== entityId),
    // 挿す先がなくなった挿入は、エンジンの検証で UNKNOWN_NODE になるので一緒に取り除く。
    inserts: config.inserts.filter((i) => i.target !== entityId),
  };
}

/** センドを取り除く。 */
export function removeSend(config: RoutingConfig, entityId: string): RoutingConfig {
  return { ...config, sends: config.sends.filter((s) => s.entity_id !== entityId) };
}

/** 手動ディレイを取り除く。 */
export function removeInputDelay(config: RoutingConfig, stripId: string): RoutingConfig {
  return { ...config, input_delays: config.input_delays.filter((d) => d.strip_id !== stripId) };
}

/** バスの名前か種類を変える。 */
export function updateBus(
  config: RoutingConfig,
  entityId: string,
  patch: Partial<{ name: string; kind: BusKind }>,
): RoutingConfig {
  return {
    ...config,
    buses: config.buses.map((b) => (b.entity_id === entityId ? { ...b, ...patch } : b)),
  };
}

/** 出力の名前かチャンネルを変える。 */
export function updateOutput(
  config: RoutingConfig,
  entityId: string,
  patch: Partial<Omit<RoutingOutput, "entity_id">>,
): RoutingConfig {
  return {
    ...config,
    outputs: config.outputs.map((o) => (o.entity_id === entityId ? { ...o, ...patch } : o)),
  };
}

/** センドの送り元、送り先、取り出す位置を変える。 */
export function updateSend(
  config: RoutingConfig,
  entityId: string,
  patch: Partial<Omit<RoutingSend, "entity_id">>,
): RoutingConfig {
  return {
    ...config,
    sends: config.sends.map((s) => (s.entity_id === entityId ? { ...s, ...patch } : s)),
  };
}

/** 遅延ノードの遅延を変える。 */
export function updateDelay(
  config: RoutingConfig,
  entityId: string,
  delayMs: number,
): RoutingConfig {
  return {
    ...config,
    delays: config.delays.map((d) => (d.entity_id === entityId ? { ...d, delay_ms: delayMs } : d)),
  };
}

/** 手動ディレイの遅延を変える。 */
export function updateInputDelay(
  config: RoutingConfig,
  stripId: string,
  delayMs: number,
): RoutingConfig {
  return {
    ...config,
    input_delays: config.input_delays.map((d) =>
      d.strip_id === stripId ? { ...d, delay_ms: delayMs } : d,
    ),
  };
}
