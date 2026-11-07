// SPDX-License-Identifier: AGPL-3.0-only
// 縦型のフェーダー（Presentational Component）。
//
// 操作の規則:
// - ドラッグで通常の調整、Ctrl を押しながらのドラッグで微調整（1/10 の感度）。
// - 通常のドラッグでは 0 dB の近くで 0 dB に吸着する。微調整では吸着しない。
// - 数値をダブルクリックすると dB 値をキーボードで直接入力できる。
// - フォーカス中は上下の矢印キーで 0.5 dB、Shift を押しながらで 0.1 dB ずつ動かす。
// - 誤操作を防ぐため、マウスホイールでは値を変えない（ホイールはページのスクロールに任せる）。
//
// 値は props で受け取り、操作の結果は onInput（操作中の値）と onCommit（操作の確定）で知らせるだけにする。

import { useRef, useState } from "preact/hooks";
import type { JSX } from "preact";
import { t } from "../../i18n/i18n";
import { formatDb, parseDb, roundDb, type GainDb } from "../../utils/db";
import type { FaderScale } from "./fader-scale";
import "./Fader.css";

/** フェーダーの props。 */
export interface FaderProps {
  /** 表示する値。null は -∞ dB。 */
  value: GainDb;
  /** つまみの位置と dB 値の対応。 */
  scale: FaderScale;
  /** 支援技術に読ませる名前（例: チャンネル名）。 */
  label: string;
  /** 操作中の値の変化。ドラッグ中は連続して呼ばれる。 */
  onInput: (value: GainDb) => void;
  /** 操作の確定（ドラッグの終了、キー操作、数値入力の確定）。 */
  onCommit: (value: GainDb) => void;
  /** 操作できないときは true。 */
  disabled?: boolean;
}

/** 矢印キー 1 回の刻み（dB）。 */
const kArrowStepDb = 0.5;
/** Shift + 矢印キー 1 回の刻み（dB）。 */
const kFineArrowStepDb = 0.1;
/** Ctrl + ドラッグの感度（通常のドラッグに対する比）。 */
const kFineDragRatio = 0.1;
/** 0 dB に吸着する範囲（±dB）。 */
const kSnapRangeDb = 0.75;
/** 吸着する値（dB）。 */
const kSnapDb = 0;
/** 高さを測れないとき（描画前など）に使うつまみの移動範囲（px）。 */
const kFallbackTrackPx = 200;

/** ドラッグ中の状態。Ctrl の押し下げが変わったら起点を取り直し、つまみが飛ばないようにする。 */
interface DragState {
  pointerId: number;
  originY: number;
  originPosition: number;
  fine: boolean;
  value: GainDb;
}

/**
 * 矢印キーで 1 段動かした値を返す。
 * -∞ から上げると有限の最小値に、有限の最小値から下げると -∞ にする。
 */
function stepValue(scale: FaderScale, value: GainDb, delta: number): GainDb {
  if (value === null) return delta > 0 ? scale.minDb : null;
  const next = roundDb(value + delta);
  if (next < scale.minDb) return scale.allowsNegInf ? null : scale.minDb;
  return Math.min(scale.maxDb, next);
}

/** 縦型のフェーダー。 */
export function Fader({ value, scale, label, onInput, onCommit, disabled = false }: FaderProps) {
  const trackRef = useRef<HTMLDivElement>(null);
  const dragRef = useRef<DragState | null>(null);
  const [editing, setEditing] = useState(false);
  const [draft, setDraft] = useState("");

  const position = scale.toPosition(value);

  const trackHeight = (): number => {
    const height = trackRef.current?.getBoundingClientRect().height ?? 0;
    return height > 0 ? height : kFallbackTrackPx;
  };

  const onPointerDown = (event: JSX.TargetedPointerEvent<HTMLDivElement>) => {
    if (disabled || event.button !== 0) return;
    event.preventDefault();
    event.currentTarget.setPointerCapture?.(event.pointerId);
    dragRef.current = {
      pointerId: event.pointerId,
      originY: event.clientY,
      originPosition: position,
      fine: event.ctrlKey,
      value,
    };
  };

  const onPointerMove = (event: JSX.TargetedPointerEvent<HTMLDivElement>) => {
    const drag = dragRef.current;
    if (drag === null || drag.pointerId !== event.pointerId) return;
    if (drag.fine !== event.ctrlKey) {
      // 感度が変わる瞬間に起点を取り直さないと、それまでの移動量に新しい感度が掛かってつまみが飛ぶ。
      drag.originY = event.clientY;
      drag.originPosition = scale.toPosition(drag.value);
      drag.fine = event.ctrlKey;
    }
    const ratio = drag.fine ? kFineDragRatio : 1;
    const moved = ((drag.originY - event.clientY) / trackHeight()) * ratio;
    const nextPosition = Math.min(1, Math.max(0, drag.originPosition + moved));
    let next = scale.fromPosition(nextPosition);
    if (!drag.fine && next !== null && Math.abs(next - kSnapDb) <= kSnapRangeDb) next = kSnapDb;
    if (next === drag.value) return;
    drag.value = next;
    onInput(next);
  };

  const endDrag = (event: JSX.TargetedPointerEvent<HTMLDivElement>) => {
    const drag = dragRef.current;
    if (drag === null || drag.pointerId !== event.pointerId) return;
    dragRef.current = null;
    onCommit(drag.value);
  };

  const onKeyDown = (event: JSX.TargetedKeyboardEvent<HTMLDivElement>) => {
    if (disabled) return;
    const step = event.shiftKey ? kFineArrowStepDb : kArrowStepDb;
    let delta: number;
    if (event.key === "ArrowUp") delta = step;
    else if (event.key === "ArrowDown") delta = -step;
    else return;
    event.preventDefault();
    const next = stepValue(scale, value, delta);
    if (next === value) return;
    onInput(next);
    onCommit(next);
  };

  const startEditing = () => {
    if (disabled) return;
    setDraft(value === null ? "-inf" : roundDb(value).toString());
    setEditing(true);
  };

  const finishEditing = (commit: boolean) => {
    setEditing(false);
    if (!commit) return;
    const parsed = parseDb(draft, scale.minDb, scale.maxDb, scale.allowsNegInf);
    // 読み取れない入力は、値を変えずに捨てる（推測して別の値を適用しない）。
    if (parsed === undefined || parsed === value) return;
    onInput(parsed);
    onCommit(parsed);
  };

  return (
    <div class={`fader${disabled ? " fader--disabled" : ""}`}>
      <div
        ref={trackRef}
        class="fader__track"
        role="slider"
        tabIndex={disabled ? -1 : 0}
        aria-label={label}
        aria-valuemin={scale.allowsNegInf ? undefined : scale.minDb}
        aria-valuemax={scale.maxDb}
        aria-valuenow={value ?? undefined}
        aria-valuetext={`${formatDb(value)} dB`}
        aria-disabled={disabled}
        onPointerDown={onPointerDown}
        onPointerMove={onPointerMove}
        onPointerUp={endDrag}
        onPointerCancel={endDrag}
        onKeyDown={onKeyDown}
      >
        {scale.marks.map((mark) => (
          <div
            key={mark}
            class={`fader__mark${mark === kSnapDb ? " fader__mark--unity" : ""}`}
            style={{ bottom: `${scale.toPosition(mark) * 100}%` }}
          >
            <span class="fader__mark-label">{mark > 0 ? `+${mark}` : mark}</span>
          </div>
        ))}
        <div class="fader__thumb" style={{ bottom: `${position * 100}%` }} />
      </div>
      {editing ? (
        <input
          class="fader__input"
          aria-label={t("fader.input_label", { name: label })}
          value={draft}
          // 編集を始めたらすぐ打てるよう、入力欄にフォーカスを移す。
          ref={(el) => el?.focus()}
          onInput={(event) => setDraft(event.currentTarget.value)}
          onKeyDown={(event) => {
            if (event.key === "Enter") finishEditing(true);
            else if (event.key === "Escape") finishEditing(false);
          }}
          onBlur={() => finishEditing(false)}
        />
      ) : (
        <button
          type="button"
          class={`fader__value${value === null ? " fader__value--neg-inf" : ""}`}
          title={t("fader.edit_hint")}
          disabled={disabled}
          onDblClick={startEditing}
        >
          {formatDb(value)}
          <span class="fader__unit">dB</span>
        </button>
      )}
    </div>
  );
}
