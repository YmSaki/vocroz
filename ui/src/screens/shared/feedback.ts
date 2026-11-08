// SPDX-License-Identifier: AGPL-3.0-only
// 操作の結果やエンジンからの通知を、トーストに変換する処理。親機と子機の両方の画面で使う。

import { isMessageKey, t, type MessageKey } from "../../i18n/i18n";
import type { CommandResult, EngineClient } from "../../ipc/engine-client";
import type { OptimisticStore } from "../../state/optimistic-store";
import type { ToastStore } from "../../state/toast-store";

/**
 * エラーコードをローカライズ済みの文言にする。未知のコードはコード付きの汎用の文言にする。
 * @param code エンジンが返したエラーコード
 */
export function errorText(code: string): string {
  const key = `error.${code}`;
  return isMessageKey(key) ? t(key) : t("error.unknown", { code });
}

/**
 * 離散コマンドが失敗したときにエラーのトーストを出す。成功したときは何もしない
 * （表示はエンジンから届く状態で更新される）。
 * @param result コマンドの結果
 * @param toasts トーストのストア
 */
export function reportCommandResult(result: CommandResult, toasts: ToastStore): void {
  if (result.ok) return;
  toasts.push("error", t("toast.command_failed", { reason: errorText(result.error_code) }));
}

/**
 * 開発中の機能が押されたときのトーストを出す。ウィンドウも状態も変えない。
 * @param featureKey 機能名の文字列リソースのキー
 * @param toasts トーストのストア
 */
export function notifyPlanned(featureKey: MessageKey, toasts: ToastStore): void {
  toasts.push("info", t("toast.planned", { feature: t(featureKey) }));
}

/**
 * 連続値の ACK が期限内に来なかったとき、楽観的な表示を戻して警告を出すようにつなぐ。
 * ACK がそろったときは、操作が終わっていれば楽観的な表示を外す。
 * @param client エンジンのクライアント
 * @param optimistic 楽観的更新のストア
 * @param toasts トーストのストア
 * @returns つなぎを外す関数
 */
export function connectContinuousFeedback(
  client: EngineClient,
  optimistic: OptimisticStore,
  toasts: ToastStore,
): () => void {
  return client.onContinuousOutcome((entityId, outcome) => {
    switch (outcome) {
      case "settled":
        optimistic.settle(entityId);
        return;
      case "timeout":
        optimistic.revert(entityId);
        toasts.push("warning", t("toast.gain_not_applied"));
        return;
    }
  });
}

/**
 * エンジンからの通知（notice）をトーストで表示するようにつなぐ。
 * @param client エンジンのクライアント
 * @param toasts トーストのストア
 * @returns つなぎを外す関数
 */
export function connectNotices(client: EngineClient, toasts: ToastStore): () => void {
  return client.subscribe((message) => {
    if (message.type !== "notice") return;
    const key = `notice.${message.code}`;
    const text = isMessageKey(key)
      ? t(key, message.params)
      : t("notice.unknown", { code: message.code });
    toasts.push(message.severity, text);
  });
}
