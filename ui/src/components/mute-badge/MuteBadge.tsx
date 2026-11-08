// SPDX-License-Identifier: AGPL-3.0-only
// 操作できないミュートの表示（Presentational Component）。
// 親機のストリップで子機側の送信ミュートを示すのに使う。親機からは解除できないので、ボタンにしない。

import "./MuteBadge.css";

/** ミュートの表示の props。 */
export interface MuteBadgeProps {
  /** 表示するか。false のときは場所だけ確保し、レイアウトをずらさない。 */
  active: boolean;
  /** ラベル（例: 子機MUTE）。 */
  text: string;
  /** 補足の説明（ツールチップ）。 */
  title: string;
}

/** 操作できないミュートの表示。 */
export function MuteBadge({ active, text, title }: MuteBadgeProps) {
  return (
    <span
      class={`mute-badge${active ? " mute-badge--on" : ""}`}
      title={active ? title : undefined}
      aria-hidden={!active}
    >
      {/* 色だけに頼らず、マイクに斜線のアイコンとラベルで示す。 */}
      <svg class="mute-badge__icon" viewBox="0 0 16 16" aria-hidden="true">
        <path d="M8 1a2 2 0 0 0-2 2v5a2 2 0 0 0 4 0V3a2 2 0 0 0-2-2z" fill="currentColor" />
        <path
          d="M4 7v1a4 4 0 0 0 8 0V7M8 12v3"
          stroke="currentColor"
          fill="none"
          stroke-width="1.5"
        />
        <path d="M2 2l12 12" stroke="currentColor" stroke-width="1.8" />
      </svg>
      {text}
    </span>
  );
}
