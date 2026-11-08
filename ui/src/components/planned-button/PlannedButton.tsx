// SPDX-License-Identifier: AGPL-3.0-only
// 今後のバージョンで実装する機能のボタン（Presentational Component）。
// 押してもウィンドウを開いたり状態を変えたりせず、onClick で呼び出し側に知らせるだけにする。
// 呼び出し側は「開発中」のトーストだけを出す。

import "./PlannedButton.css";

/** 開発中のボタンの props。 */
export interface PlannedButtonProps {
  /** 機能名。 */
  text: string;
  /** 「開発中」であることを示す補足（ツールチップ）。 */
  title: string;
  /** 押されたとき。 */
  onClick: () => void;
}

/** 開発中の機能のボタン。 */
export function PlannedButton({ text, title, onClick }: PlannedButtonProps) {
  return (
    <button
      type="button"
      class="planned-button"
      title={title}
      data-planned="true"
      onClick={onClick}
    >
      {text}
    </button>
  );
}
