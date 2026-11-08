// SPDX-License-Identifier: AGPL-3.0-only
// フェーダーのつまみの位置（0〜1）と dB 値の対応（フェーダーカーブ）。
// ミキサーのフェーダーは実機のコンソールと同じく 0 dB 付近を細かく、下側を粗くする。
// 子機の送信レベルは範囲が狭いので一様にする。

import { roundDb, type GainDb } from "../../utils/db";

/** フェーダーのつまみの位置と dB 値の対応。 */
export interface FaderScale {
  /** 有限の最小値（dB）。 */
  minDb: number;
  /** 最大値（dB）。 */
  maxDb: number;
  /** つまみを一番下にしたとき -∞ dB にするか。 */
  allowsNegInf: boolean;
  /** 目盛りを描く dB 値。 */
  marks: readonly number[];
  /** dB 値をつまみの位置（0〜1）にする。 */
  toPosition(db: GainDb): number;
  /** つまみの位置（0〜1）を dB 値にする。 */
  fromPosition(position: number): GainDb;
}

/** 位置と dB 値の折れ線の頂点。 */
type CurvePoint = readonly [position: number, db: number];

/**
 * ミキサーのフェーダーカーブ（-∞〜+10 dB）。
 * 0 dB をつまみの 3/4 の高さに置き、よく使う -12〜+10 dB に全体の半分を割り当てる。
 * 一番下（位置 kNegInfPosition 未満）は -∞ dB にする。
 */
const kMixerCurve: readonly CurvePoint[] = [
  [0.02, -60],
  [0.25, -30],
  [0.5, -12],
  [0.75, 0],
  [1, 10],
];
/** これより下の位置は -∞ dB とする。 */
const kNegInfPosition = 0.02;

function interpolate(curve: readonly CurvePoint[], x: number, from: 0 | 1, to: 0 | 1): number {
  for (let i = 1; i < curve.length; i++) {
    const a = curve[i - 1]!;
    const b = curve[i]!;
    if (x <= b[from] || i === curve.length - 1) {
      const ratio = (x - a[from]) / (b[from] - a[from]);
      return a[to] + (b[to] - a[to]) * Math.min(1, Math.max(0, ratio));
    }
  }
  return curve[curve.length - 1]![to];
}

/** 親機のチャンネルと Master のフェーダー（-∞〜+10 dB）。 */
export const kMixerFaderScale: FaderScale = {
  minDb: -60,
  maxDb: 10,
  allowsNegInf: true,
  marks: [10, 0, -10, -20, -30, -60],
  toPosition(db) {
    if (db === null) return 0;
    return interpolate(kMixerCurve, Math.min(10, Math.max(-60, db)), 1, 0);
  },
  fromPosition(position) {
    if (position < kNegInfPosition) return null;
    return roundDb(interpolate(kMixerCurve, Math.min(1, position), 0, 1));
  },
};

/**
 * 一様なフェーダーカーブを作る。
 * @param minDb 最小値（dB）
 * @param maxDb 最大値（dB）
 * @param marks 目盛りを描く dB 値
 */
export function linearFaderScale(
  minDb: number,
  maxDb: number,
  marks: readonly number[],
): FaderScale {
  return {
    minDb,
    maxDb,
    allowsNegInf: false,
    marks,
    toPosition(db) {
      const value = db ?? minDb;
      return (Math.min(maxDb, Math.max(minDb, value)) - minDb) / (maxDb - minDb);
    },
    fromPosition(position) {
      const clamped = Math.min(1, Math.max(0, position));
      return roundDb(minDb + (maxDb - minDb) * clamped);
    },
  };
}

/** 子機の送信レベル（-60〜+12 dB）。 */
export const kSendLevelScale: FaderScale = linearFaderScale(
  -60,
  12,
  [12, 0, -12, -24, -36, -48, -60],
);
