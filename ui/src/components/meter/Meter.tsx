// SPDX-License-Identifier: AGPL-3.0-only
// レベルメーター（Presentational Component）。
//
// メーターは高頻度で変わるので DOM を更新せず、Canvas に requestAnimationFrame で描く。
// 値は props の read 関数で描画のたびに読み、画面全体の再描画を起こさない。
// 最後の値から 500 ms 以上新しい値が届かなければ、緑のバーを止めたままにせず減衰させる
// （信号が止まっているのに音が出ているように見せないため）。

import { useEffect, useRef } from "preact/hooks";
import "./Meter.css";

/** メーターに渡す値。 */
export interface MeterSample {
  /** ピーク（dBFS）。null は無音。 */
  levelDb: number | null;
  /** 値を受け取ってからの経過時間（ms）。 */
  ageMs: number;
}

/** メーターの props。 */
export interface MeterProps {
  /** 現在の値を返す関数。値がまだなければ undefined。 */
  read: () => MeterSample | undefined;
  /** 支援技術に読ませる名前。 */
  label: string;
}

/** 表示する最小レベル（dBFS）。これより小さい値はバーを描かない。 */
export const kMeterFloorDb = -60;
/** 値が古いとみなすまでの時間（ms）。 */
export const kMeterStaleMs = 500;
/** 古くなった値を減衰させる速さ（dB / s）。 */
const kStaleDecayDbPerSecond = 120;
/** 黄色にする閾値（dBFS）。 */
const kWarnDb = -12;
/** 赤にする閾値（dBFS）。 */
const kClipDb = -3;

/**
 * 表示するレベル（dBFS）を求める。古い値は経過時間に応じて減衰させる。
 * @param sample メーターの値
 * @returns 表示するレベル。描かないときは null
 */
export function displayLevel(sample: MeterSample | undefined): number | null {
  if (sample === undefined || sample.levelDb === null) return null;
  if (sample.ageMs <= kMeterStaleMs) return sample.levelDb;
  const decayed = sample.levelDb - ((sample.ageMs - kMeterStaleMs) / 1000) * kStaleDecayDbPerSecond;
  return decayed <= kMeterFloorDb ? null : decayed;
}

/** レベル（dBFS）をバーの高さの比（0〜1）にする。 */
function toRatio(levelDb: number): number {
  return Math.min(1, Math.max(0, (levelDb - kMeterFloorDb) / -kMeterFloorDb));
}

/** 縦型のレベルメーター。 */
export function Meter({ read, label }: MeterProps) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  // read は描画のたびに変わってよいので、ループを作り直さず最新のものを参照する。
  const readRef = useRef(read);
  readRef.current = read;

  useEffect(() => {
    const canvas = canvasRef.current;
    const context = canvas?.getContext("2d");
    if (canvas == null || context == null) return;
    // 色は CSS 変数で決める。毎フレーム読むと重いので、描画を始めるときに 1 回だけ読む。
    const styles = getComputedStyle(canvas);
    const color = (name: string, fallback: string) =>
      styles.getPropertyValue(name).trim() || fallback;
    const colors = {
      background: color("--meter-bg", "#111"),
      ok: color("--meter-ok", "#3c6"),
      warn: color("--meter-warn", "#ec3"),
      clip: color("--meter-clip", "#e33"),
    };
    let frame = 0;
    const draw = () => {
      const width = canvas.clientWidth;
      const height = canvas.clientHeight;
      const scale = window.devicePixelRatio || 1;
      if (canvas.width !== Math.round(width * scale)) canvas.width = Math.round(width * scale);
      if (canvas.height !== Math.round(height * scale)) canvas.height = Math.round(height * scale);
      context.setTransform(scale, 0, 0, scale, 0, 0);
      context.fillStyle = colors.background;
      context.fillRect(0, 0, width, height);
      const level = displayLevel(readRef.current());
      if (level !== null) {
        const barHeight = toRatio(level) * height;
        context.fillStyle =
          level >= kClipDb ? colors.clip : level >= kWarnDb ? colors.warn : colors.ok;
        context.fillRect(0, height - barHeight, width, barHeight);
      }
      frame = requestAnimationFrame(draw);
    };
    frame = requestAnimationFrame(draw);
    return () => cancelAnimationFrame(frame);
  }, []);

  // Canvas は外側の要素いっぱいに広げる。Canvas の解像度を変えても要素の大きさが変わらないよう、
  // 大きさは外側の要素で決める（Canvas 自身の大きさで決めると、解像度の変更で伸び続ける）。
  return (
    <div class="meter" role="img" aria-label={label}>
      <canvas ref={canvasRef} class="meter__canvas" />
    </div>
  );
}
