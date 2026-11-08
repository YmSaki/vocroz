// SPDX-License-Identifier: AGPL-3.0-only
// フェーダーなどの連続値の操作を、楽観的更新と間引き送信につなぐフック。
//
// ドラッグ中は手元の値を表示し（楽観的更新）、エンジンへは 60 Hz に間引いて送る。
// 操作が終わったら残りの値を送り、ACK がそろった時点でエンジンの値の表示に戻す。
// ACK が来なかったときの戻しと警告は connectContinuousFeedback が行う。

import type { EngineClient } from "../../ipc/engine-client";
import type { GainDb } from "../../ipc/messages";
import type { OptimisticStore } from "../../state/optimistic-store";
import { useStore } from "../../state/use-store";

/** 連続値の操作のハンドラー。 */
export interface ContinuousGain {
  /** 表示する値。操作中は手元の値、それ以外はエンジンの値。 */
  displayValue: (entityId: string, engineValue: GainDb) => GainDb;
  /** 操作中の値の変化。 */
  onInput: (entityId: string, value: GainDb) => void;
  /** 操作の確定。 */
  onCommit: (entityId: string, value: GainDb) => void;
}

/**
 * 連続値の操作のハンドラーを返す。
 * @param client エンジンのクライアント
 * @param optimistic 楽観的更新のストア
 */
export function useContinuousGain(
  client: EngineClient,
  optimistic: OptimisticStore,
): ContinuousGain {
  const overlay = useStore(optimistic);
  return {
    displayValue: (entityId, engineValue) => {
      const entry = overlay.get(entityId);
      return entry === undefined ? engineValue : entry.value;
    },
    onInput: (entityId, value) => {
      optimistic.begin(entityId, value);
      client.setContinuous(entityId, value);
    },
    onCommit: (entityId, value) => {
      optimistic.begin(entityId, value);
      client.setContinuous(entityId, value);
      client.flush(entityId);
      optimistic.release(entityId);
    },
  };
}
