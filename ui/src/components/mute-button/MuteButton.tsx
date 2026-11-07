// SPDX-License-Identifier: AGPL-3.0-only
// ミュートのボタン（Presentational Component）。
// ミュートの理由は色だけでなくラベルで区別する（色覚に頼らないため）。
// 押した結果の表示はエンジンの ACK を待ってから変わるので、押したことは pending で示す。

import "./MuteButton.css";

/** ミュートのボタンの props。 */
export interface MuteButtonProps {
  /** ミュート中か（エンジンが確定した値）。 */
  muted: boolean;
  /** ボタンのラベル（例: MUTE、SAFETY MUTE）。 */
  text: string;
  /** 支援技術に読ませる名前（例: 「Vo1 のミュート」）。 */
  label: string;
  /** エンジンが安全のために設定したミュートなら true。オペレーターのミュートと色とラベルを変える。 */
  safety?: boolean;
  /** コマンドを送って ACK を待っている間は true。 */
  pending?: boolean;
  /** 押されたとき。 */
  onToggle: () => void;
}

/** ミュートのボタン。 */
export function MuteButton({
  muted,
  text,
  label,
  safety = false,
  pending = false,
  onToggle,
}: MuteButtonProps) {
  const classes = [
    "mute-button",
    muted ? "mute-button--on" : "",
    muted && safety ? "mute-button--safety" : "",
    pending ? "mute-button--pending" : "",
  ]
    .filter((c) => c !== "")
    .join(" ");
  return (
    <button
      type="button"
      class={classes}
      aria-pressed={muted}
      aria-label={label}
      aria-busy={pending}
      onClick={onToggle}
    >
      {text}
    </button>
  );
}
