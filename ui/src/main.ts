// SPDX-License-Identifier: AGPL-3.0-only
// UI のエントリポイント。
// ネイティブ側から受け取ったモードで画面を選び、IPC クライアントとストアをつないでマウントする。
// JUCE の WebBrowserComponent の外（ブラウザでの開発時）では、開発用のフェイクエンジンにつなぐ。

import { h, render } from "preact";
import { detectLocale, setLocale } from "./i18n/i18n";
import { EngineClient } from "./ipc/engine-client";
import { findJuce, JuceTransport } from "./ipc/juce-transport";
import { JuceWindowControl } from "./ipc/juce-window";
import { readLaunchConfig, type UiMode } from "./ipc/launch-config";
import type { Transport } from "./ipc/transport";
import { ClientScreen } from "./screens/client/ClientScreen";
import { MixerScreen } from "./screens/mixer/MixerScreen";
import { MediaScreen } from "./screens/media/MediaScreen";
import { RoutingScreen } from "./screens/routing/RoutingScreen";
import {
  connectContinuousFeedback,
  connectNotices,
  connectPluginNotices,
} from "./screens/shared/feedback";
import { ClientStore } from "./state/client-store";
import { LinkStatsStore } from "./state/link-stats-store";
import { MeterStore } from "./state/meter-store";
import { MixerStore } from "./state/mixer-store";
import { OptimisticStore } from "./state/optimistic-store";
import { PluginStore } from "./state/plugin-store";
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
  const client = new EngineClient(transport, undefined, config.requestPrefix);
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
    const linkStats = new LinkStatsStore();
    const plugins = new PluginStore();
    client.subscribe((message) => {
      store.apply(message);
      plugins.apply(message);
      if (message.type === "link_stats") linkStats.apply(message);
    });
    // JUCE 上では、ルーティング画面とメディア画面は別の窓で開く（docs/adr/0038-routing-window.md、
    // docs/adr/0043-recording-and-media-player.md）。窓はネイティブ側が開き、その窓の UI は
    // screen=routing か screen=media で起動して自分でエンジンの状態を受け取る。
    const windows = juce !== undefined ? new JuceWindowControl(juce.backend) : undefined;
    if (windows !== undefined && config.screen === "media") {
      render(
        h(MediaScreen, {
          client,
          store,
          toasts,
          onClose: () => windows.close(),
          inOwnWindow: true,
        }),
        root,
      );
      client.hello();
      return;
    }
    if (windows !== undefined && config.screen === "routing") {
      render(
        h(RoutingScreen, {
          client,
          store,
          plugins,
          optimistic,
          toasts,
          onClose: () => windows.close(),
          inOwnWindow: true,
        }),
        root,
      );
      client.hello();
      return;
    }
    // プラグインの失敗はミキサーの窓で知らせる。ルーティングの窓でも知らせると、2 つの窓に同じ通知が出るため。
    connectPluginNotices(plugins, toasts, (insertId) => {
      const insert = store.get()?.routing.applied.inserts.find((i) => i.entity_id === insertId);
      if (insert === undefined) return insertId;
      return plugins.find(insert.plugin_id)?.name ?? insert.plugin_id;
    });
    // ブラウザでの開発時は窓を開けないので、同じ窓で切り替える。
    // ストアは画面をまたいで共有するので、切り替えてもエンジンの状態を取り直さない。
    const showMixer = (): void =>
      render(
        h(MixerScreen, {
          client,
          store,
          optimistic,
          meters,
          linkStats,
          toasts,
          onOpenRouting: windows !== undefined ? () => windows.openRouting() : showRouting,
          onOpenMedia: windows !== undefined ? () => windows.openMedia() : showMedia,
        }),
        root,
      );
    const showRouting = (): void =>
      render(
        h(RoutingScreen, { client, store, plugins, optimistic, toasts, onClose: showMixer }),
        root,
      );
    const showMedia = (): void =>
      render(h(MediaScreen, { client, store, toasts, onClose: showMixer }), root);
    showMixer();
  }
  // 購読をつないでから全状態を要求し、スナップショットを取りこぼさないようにする。
  client.hello();
}

void main();
