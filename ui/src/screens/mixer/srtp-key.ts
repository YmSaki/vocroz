// SPDX-License-Identifier: AGPL-3.0-only
// 外部出力の SRTP の鍵を作る。
// 形式は SDES（RFC 4568）の a=crypto の inline: と同じで、マスターキー 16 バイトとマスターソルト 14 バイトを
// 連結した 30 バイトの Base64（docs/adr/0040-srtp-external-output.md）。受信機の設定画面にそのまま貼り付けられる。
// 作った鍵はエンジンへ送るだけで、エンジンは鍵を UI へ返さない。受信機へ貼るための写しはこの画面でだけ扱う。

/** マスターキーとマスターソルトの合計の長さ（バイト）。 */
const kSrtpKeyBytes = 30;

/**
 * 新しい鍵を inline: 形式で作る。
 * @param random 暗号論的な乱数でバイト列を埋める関数。テストでは決定的な関数を渡す
 */
export function generateSrtpKey(
  random: (bytes: Uint8Array) => void = (bytes) => crypto.getRandomValues(bytes),
): string {
  const bytes = new Uint8Array(kSrtpKeyBytes);
  random(bytes);
  let binary = "";
  for (const b of bytes) binary += String.fromCharCode(b);
  return `inline:${btoa(binary)}`;
}
