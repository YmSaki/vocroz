// SPDX-License-Identifier: AGPL-3.0-only
// 離散コマンドを送り、ACK を待つ間の状態（pending）を返すフック。
// 表示は ACK のあとにエンジンから届く状態で変わるので、ここでは待っていることだけを扱う。

import { useState } from "preact/hooks";
import type { EngineClient } from "../../ipc/engine-client";
import type { DiscreteCommandBody } from "../../ipc/messages";
import type { ToastStore } from "../../state/toast-store";
import { reportCommandResult } from "./feedback";

/** 離散コマンドの送信のハンドラー。 */
export interface CommandSender {
  /** entity に ACK 待ちのコマンドがあるか。 */
  isPending: (entityId: string) => boolean;
  /** コマンドを送る。同じ entity の ACK を待っている間は送らない（連打で逆の操作が重ならないように）。 */
  send: (entityId: string, revision: number, body: DiscreteCommandBody) => void;
}

/**
 * 離散コマンドの送信のハンドラーを返す。
 * @param client エンジンのクライアント
 * @param toasts 失敗を知らせるトーストのストア
 */
export function useCommand(client: EngineClient, toasts: ToastStore): CommandSender {
  const [pending, setPending] = useState<ReadonlySet<string>>(new Set());
  return {
    isPending: (entityId) => pending.has(entityId),
    send: (entityId, revision, body) => {
      if (pending.has(entityId)) return;
      setPending((current) => new Set(current).add(entityId));
      void client.sendCommand(entityId, revision, body).then((result) => {
        setPending((current) => {
          const next = new Set(current);
          next.delete(entityId);
          return next;
        });
        reportCommandResult(result, toasts);
      });
    },
  };
}
