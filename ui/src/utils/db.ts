// SPDX-License-Identifier: AGPL-3.0-only
// dB 値の表示と入力の変換。フェーダーと送信レベルで同じ規則を使う。

/**
 * ゲイン（dB）。null は -∞ dB を表す。
 * -∞ dB は音量値であり、ミュートとは別に扱う（ミュートはフラグで表す）。
 */
export type GainDb = number | null;

/** dB を表示するときの小数の桁数。操作の最小刻み（0.1 dB）に合わせる。 */
const kDecimals = 1;

/**
 * dB 値を表示用の文字列にする。null は -∞ を表す。
 * @param db dB 値
 * @returns 例: "+3.5"、"0.0"、"-12.0"、"-∞"
 */
export function formatDb(db: GainDb): string {
  if (db === null) return "-∞";
  const rounded = roundDb(db);
  // -0.0 を表示しないよう、0 は符号なしにする。
  if (rounded === 0) return (0).toFixed(kDecimals);
  return `${rounded > 0 ? "+" : ""}${rounded.toFixed(kDecimals)}`;
}

/**
 * dB 値を操作の最小刻み（0.1 dB）に丸める。
 * @param db dB 値
 */
export function roundDb(db: number): number {
  const factor = 10 ** kDecimals;
  const rounded = Math.round(db * factor) / factor;
  return rounded === 0 ? 0 : rounded;
}

/**
 * 利用者が入力した文字列を dB 値にする。
 * "-inf"、"-∞" は -∞（null）、数値は min〜max に収める。
 * @param text 入力された文字列
 * @param min 最小値（dB）
 * @param max 最大値（dB）
 * @param allowNegInf -∞ を受け付けるか
 * @returns dB 値。読み取れなければ undefined
 */
export function parseDb(
  text: string,
  min: number,
  max: number,
  allowNegInf: boolean,
): GainDb | undefined {
  // 全角の数字や記号、Unicode のマイナス記号も受け付ける（日本語入力のままでも打てるように）。
  const normalized = text
    .normalize("NFKC")
    .trim()
    .toLowerCase()
    .replace(/−/g, "-")
    .replace(/\s*db$/, "");
  if (normalized === "-inf" || normalized === "-∞" || normalized === "-infinity") {
    return allowNegInf ? null : min;
  }
  if (!/^[+-]?(\d+(\.\d*)?|\.\d+)$/.test(normalized)) return undefined;
  const value = Number(normalized);
  if (!Number.isFinite(value)) return undefined;
  return roundDb(Math.min(max, Math.max(min, value)));
}
