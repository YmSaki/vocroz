// SPDX-License-Identifier: AGPL-3.0-only
// 窓の操作をネイティブ側（modules/webui）に求める。
// 親機モードのルーティング画面とメディア画面は別の窓で開く。窓を開いたり閉じたりできるのはネイティブ側だけなので、
// UI はイベントで求めるだけにする（docs/adr/0038-routing-window.md、
// docs/adr/0043-recording-and-media-player.md）。

import type { JuceBackend } from "./juce-transport";

/** 窓の操作を求めるイベントの ID。modules/webui の kWindowEventId と同じ値。 */
export const kWindowEventId = "vocroz.window";

/** 窓の操作。 */
export interface WindowControl {
  /** ルーティングの窓を開く。開いていれば前面に出す。 */
  openRouting(): void;
  /** メディアの窓を開く。開いていれば前面に出す。 */
  openMedia(): void;
  /** この窓を閉じる。 */
  close(): void;
}

/** JUCE のネイティブイベントで窓の操作を求める WindowControl。 */
export class JuceWindowControl implements WindowControl {
  /** @param backend JUCE のバックエンド */
  constructor(private readonly backend: JuceBackend) {}

  openRouting(): void {
    this.backend.emitEvent(kWindowEventId, "open_routing");
  }

  openMedia(): void {
    this.backend.emitEvent(kWindowEventId, "open_media");
  }

  close(): void {
    this.backend.emitEvent(kWindowEventId, "close");
  }
}
