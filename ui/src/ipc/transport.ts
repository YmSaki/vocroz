// SPDX-License-Identifier: AGPL-3.0-only
// UI とネイティブ側の間でメッセージを運ぶ経路の抽象。
// control-protocol の MessageChannel ポートに対応する UI 側の定義で、
// 本番では JUCE のブリッジ（juce-transport.ts）、開発とテストではフェイクエンジンが実装する。

import type { EngineToUiMessage, UiToEngineMessage } from "./messages";

/** メッセージの受信を待つリスナー。 */
export type MessageListener = (message: EngineToUiMessage) => void;

/** UI とエンジンの間の双方向のメッセージ経路。 */
export interface Transport {
  /** エンジンへメッセージを送る。送信の成否は返さない（応答は ACK で受け取る）。 */
  send(message: UiToEngineMessage): void;
  /**
   * エンジンからのメッセージを購読する。
   * @returns 購読を解除する関数
   */
  subscribe(listener: MessageListener): () => void;
}
