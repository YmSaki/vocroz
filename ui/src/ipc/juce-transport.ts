// SPDX-License-Identifier: AGPL-3.0-only
// JUCE の WebBrowserComponent のネイティブイベントを使う Transport の実装。
// ネイティブ側（modules/webui）は JSON を中継するだけで、検証は control-protocol が行う
// （docs/adr/0005-ui-juce-webview.md）。

import type { EngineToUiMessage, UiToEngineMessage } from "./messages";
import type { MessageListener, Transport } from "./transport";

/** UI からネイティブ側へ送るイベントの ID。modules/webui と同じ値を使う。 */
export const kToNativeEventId = "vocroz.to_engine";
/** ネイティブ側から UI へ届くイベントの ID。modules/webui と同じ値を使う。 */
export const kFromNativeEventId = "vocroz.from_engine";

/** JUCE が window.__JUCE__ に注入するバックエンドのうち、使う部分だけの型。 */
export interface JuceBackend {
  emitEvent(eventId: string, payload: unknown): void;
  addEventListener(eventId: string, handler: (payload: unknown) => void): unknown;
  removeEventListener(token: unknown): void;
}

/** JUCE が注入するグローバルオブジェクトの型。 */
export interface JuceGlobal {
  backend: JuceBackend;
  /** WebBrowserComponent::Options::withInitialisationData で渡した値。 */
  initialisationData?: Record<string, unknown>;
}

/** ページが JUCE の WebBrowserComponent 上で動いていれば、そのグローバルを返す。 */
export function findJuce(): JuceGlobal | undefined {
  return (window as unknown as { __JUCE__?: JuceGlobal }).__JUCE__;
}

/** JUCE のネイティブイベントでエンジンと通信する Transport。 */
export class JuceTransport implements Transport {
  private readonly listeners = new Set<MessageListener>();

  /**
   * @param backend JUCE のバックエンド
   */
  constructor(private readonly backend: JuceBackend) {
    backend.addEventListener(kFromNativeEventId, (payload) => {
      // ネイティブ側は文字列の JSON を渡す。形の検証はエンジン側の信頼境界で済んでいるので、
      // ここでは JSON として読めないものだけを捨てる。
      let message: EngineToUiMessage;
      try {
        message = (
          typeof payload === "string" ? JSON.parse(payload) : payload
        ) as EngineToUiMessage;
      } catch {
        return;
      }
      for (const listener of this.listeners) listener(message);
    });
  }

  send(message: UiToEngineMessage): void {
    this.backend.emitEvent(kToNativeEventId, JSON.stringify(message));
  }

  subscribe(listener: MessageListener): () => void {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }
}
