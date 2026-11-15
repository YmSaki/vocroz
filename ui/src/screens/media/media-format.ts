// SPDX-License-Identifier: AGPL-3.0-only
// メディア画面の表示用の整形。経過時間と再生位置を時計の形（m:ss、1 時間を超えたら h:mm:ss）で示す。

/**
 * ms を時計の形の文字列にする。秒未満は切り捨てる。
 * @param ms 長さ（ms）。負の値は 0 として扱う
 * @returns 1 時間未満は m:ss、それ以上は h:mm:ss
 */
export function formatClock(ms: number): string {
  const totalSeconds = Math.floor(Math.max(0, ms) / 1000);
  const hours = Math.floor(totalSeconds / 3600);
  const minutes = Math.floor((totalSeconds % 3600) / 60);
  const seconds = totalSeconds % 60;
  const ss = String(seconds).padStart(2, "0");
  if (hours === 0) return `${minutes}:${ss}`;
  return `${hours}:${String(minutes).padStart(2, "0")}:${ss}`;
}

/**
 * 録る対象を切り替えた後の一覧を返す。既にある対象の順は変えず、新しい対象は末尾に足す。
 * 一覧の順がファイルの番号になるので、ほかの対象の番号を動かさないため。
 * @param sources いまの録る対象
 * @param id 切り替える対象
 * @param selected 録る対象にするか
 */
export function toggleSource(sources: readonly string[], id: string, selected: boolean): string[] {
  const without = sources.filter((s) => s !== id);
  return selected ? [...without, id] : without;
}
