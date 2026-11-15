// SPDX-License-Identifier: AGPL-3.0-only
// メディア画面（Container）。録音（形式、保存先、録る対象、開始と停止、経過時間、欠落の警告）と、
// メディアプレイヤー（ライブラリ、流す先、再生、一時停止、停止、シーク）の 2 つの区画を置く
// （docs/adr/0043-recording-and-media-player.md）。
// JUCE 上ではルーティング画面と同じく別の窓で開き、録音とプレイヤーの操作はこの窓だけにある。

import { t } from "../../i18n/i18n";
import type { EngineClient } from "../../ipc/engine-client";
import type { ChannelStrip, RoutingBus } from "../../ipc/messages";
import type { MixerStore } from "../../state/mixer-store";
import type { ToastStore } from "../../state/toast-store";
import { useStore } from "../../state/use-store";
import { stripInputLabel } from "../shared/strip-input";
import { ToastLayer } from "../shared/ToastLayer";
import { useCommand } from "../shared/use-command";
import { PlayerPanel } from "./PlayerPanel";
import { RecorderPanel, type SourceOption } from "./RecorderPanel";
import "./MediaScreen.css";

/** メディア画面の props。依存はコンポジション（main.ts）から注入する。 */
export interface MediaScreenProps {
  client: EngineClient;
  store: MixerStore;
  toasts: ToastStore;
  /** 画面を閉じる。同じ窓で開いたときはミキサー画面に戻り、別の窓で開いたときは窓を閉じる。 */
  onClose: () => void;
  /** 別の窓で開いているか。閉じるボタンの文言を変える。 */
  inOwnWindow?: boolean;
}

/**
 * 録る対象と流す先の表示名を引く関数を作る。名前のないバスやストリップは ID で示す。
 * 1 台の子機の複数の入力は同じ名前になるので、ミキサー画面と同じ入力のラベルを添えて見分ける。
 */
function makeLabelOf(
  buses: readonly RoutingBus[],
  channels: readonly ChannelStrip[],
): (entityId: string) => string {
  const labels = new Map<string, string>();
  labels.set("master", t("mixer.master"));
  for (const b of buses) labels.set(b.entity_id, b.name || b.entity_id);
  for (const c of channels) {
    const input = stripInputLabel(c);
    const name = c.name || c.short_id;
    labels.set(c.entity_id, input === "" ? name : `${name}（${input}）`);
  }
  return (entityId) => labels.get(entityId) ?? entityId;
}

/** メディア画面。 */
export function MediaScreen({
  client,
  store,
  toasts,
  onClose,
  inOwnWindow = false,
}: MediaScreenProps) {
  const state = useStore(store);
  const command = useCommand(client, toasts);

  if (state === null) {
    return <div class="screen-loading">{t("common.connecting_engine")}</div>;
  }

  const { recorder, media_player: player, media_library: library, channels } = state;
  const buses = state.routing.applied.buses;
  const labelOf = makeLabelOf(buses, channels);
  const option = (entityId: string): SourceOption => ({ entityId, label: labelOf(entityId) });
  const targets = [option("master"), ...buses.map((b) => option(b.entity_id))];
  const sources = [...targets, ...channels.map((c) => option(c.entity_id))];
  const sendPlayer = (body: Parameters<typeof command.send>[2]) =>
    command.send(player.entity_id, player.revision, body);

  return (
    <div class="media-screen">
      <header class="media-header">
        <button type="button" onClick={onClose}>
          {t(inOwnWindow ? "media.close" : "media.back")}
        </button>
        <h1 class="media-header__title">{t("media.title")}</h1>
      </header>
      <div class="media-body">
        <RecorderPanel
          recorder={recorder}
          sources={sources}
          labelOf={labelOf}
          pending={command.isPending(recorder.entity_id)}
          onSetConfig={(config) =>
            command.send(recorder.entity_id, recorder.revision, {
              type: "set_recorder_config",
              config,
            })
          }
          onSetRecording={(recording) =>
            command.send(recorder.entity_id, recorder.revision, {
              type: "set_recording",
              recording,
            })
          }
        />
        <PlayerPanel
          player={player}
          library={library}
          targets={targets}
          pending={command.isPending(player.entity_id)}
          onLoad={(path) => sendPlayer({ type: "load_media", path })}
          onTransport={(transport) => sendPlayer({ type: "set_media_transport", transport })}
          onSeek={(positionMs) =>
            sendPlayer({ type: "seek_media", position_ms: Math.round(positionMs) })
          }
          onSetTarget={(target) => sendPlayer({ type: "set_media_target", target })}
          onRefresh={() => sendPlayer({ type: "refresh_media_library" })}
        />
      </div>
      <ToastLayer toasts={toasts} rightInset={0} />
    </div>
  );
}
