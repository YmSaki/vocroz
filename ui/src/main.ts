// SPDX-License-Identifier: AGPL-3.0-only
// UI のエントリポイント。
// ネイティブ側から受け取ったモードで画面を選び、IPC クライアントとストアをつないでマウントする。
// JUCE の WebBrowserComponent の外（ブラウザでの開発時）では、開発用のフェイクエンジンにつなぐ。

import { h, render } from "preact";
import { detectLocale, setLocale } from "./i18n/i18n";
import { EngineClient } from "./ipc/engine-client";
import { findJuce, JuceTransport } from "./ipc/juce-transport";
import { readLaunchConfig, type UiMode } from "./ipc/launch-config";
import type { Transport } from "./ipc/transport";
import { ClientScreen } from "./screens/client/ClientScreen";
import { MixerScreen } from "./screens/mixer/MixerScreen";
import { connectContinuousFeedback, connectNotices } from "./screens/shared/feedback";
import { ClientStore } from "./state/client-store";
import { MeterStore } from "./state/meter-store";
import { MixerStore } from "./state/mixer-store";
import { OptimisticStore } from "./state/optimistic-store";
import { ToastStore } from "./state/toast-store";
import "./theme.css";

/**
 * フェイクエンジンを作る。本番の成果物に含めないよう、必要なときだけ読み込む。
 * 開発時はブラウザのコンソールから window.vocrozFake で異常系を再現できる。
 */
async function createFakeTransport(mode: UiMode): Promise<Transport> {
  const engine =
    mode === "client"
      ? new (await import("./ipc/fake-engine/fake-client-engine")).FakeClientEngine()
      : new (await import("./ipc/fake-engine/fake-mixer-engine")).FakeMixerEngine();
  (window as unknown as { vocrozFake: unknown }).vocrozFake = engine;
  return engine;
}

async function main(): Promise<void> {
  const juce = findJuce();
  const config = readLaunchConfig(juce?.initialisationData, window.location.search);
  setLocale(detectLocale(config.lang, navigator.languages));
  document.documentElement.dataset["mode"] = config.mode;

  const transport =
    juce !== undefined ? new JuceTransport(juce.backend) : await createFakeTransport(config.mode);
  const client = new EngineClient(transport);
  const meters = new MeterStore();
  const optimistic = new OptimisticStore();
  const toasts = new ToastStore();
  connectContinuousFeedback(client, optimistic, toasts);
  connectNotices(client, toasts);
  client.subscribe((message) => {
    if (message.type === "meters") meters.apply(message);
  });

  const root = document.getElementById("app");
  if (root === null) return;
  if (config.mode === "client") {
    const store = new ClientStore();
    client.subscribe((message) => store.apply(message));
    render(
      h(ClientScreen, { client, store, optimistic, meters, toasts, initialInvite: config.invite }),
      root,
    );
  } else {
    const store = new MixerStore();
    client.subscribe((message) => store.apply(message));
    render(h(MixerScreen, { client, store, optimistic, meters, toasts }), root);
  }
  // 購読をつないでから全状態を要求し、スナップショットを取りこぼさないようにする。
  client.hello();
}

void main();
