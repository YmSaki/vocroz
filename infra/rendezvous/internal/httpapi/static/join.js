// SPDX-License-Identifier: AGPL-3.0-only
// 招待 URL の着地ページのスクリプト。
// 招待コードは URL のフラグメントから読み、サーバーには送らない。
// 受け付けるのは招待コードに使う文字だけで、それ以外を含むフラグメントは無視する
// （vocroz:// に任意の文字列を渡さないため。最終的な形式の検証はアプリの session が行う）。
"use strict";

(() => {
  const messages = {
    ja: {
      title: "VOCROZ への招待",
      lead: "VOCROZ のセッションに招待されています。",
      open: "VOCROZ で開く",
      notInstalled: "VOCROZ が入っていない場合は、先にダウンロードしてください。",
      download: "VOCROZ をダウンロード",
      codeHeading: "招待コード",
      codeHint: "ボタンで開けない場合は、VOCROZ の「招待コードで参加」にこのコードを貼り付けてください。",
      copy: "コピー",
      copied: "コピーしました",
      noCode: "招待コードが見つかりません。招待した人から届いたリンクをそのまま開いてください。",
    },
    en: {
      title: "You're invited to VOCROZ",
      lead: "You have been invited to a VOCROZ session.",
      open: "Open in VOCROZ",
      notInstalled: "If VOCROZ is not installed, download it first.",
      download: "Download VOCROZ",
      codeHeading: "Invite code",
      codeHint: "If the button does not work, paste this code into \"Join with invite code\" in VOCROZ.",
      copy: "Copy",
      copied: "Copied",
      noCode: "No invite code was found. Open the link you received exactly as it was sent.",
    },
  };

  // 招待コードは Crockford Base32 の 10 文字。表示用のハイフンや小文字も受け付ける。
  const codePattern = /^[0-9A-Za-z-]{10,16}$/;

  const lang = (navigator.language || "ja").toLowerCase().startsWith("ja") ? "ja" : "en";
  const text = messages[lang];

  document.addEventListener("DOMContentLoaded", () => {
    document.documentElement.lang = lang;
    document.title = text.title;
    for (const el of document.querySelectorAll("[data-i18n]")) {
      el.textContent = text[el.dataset.i18n];
    }

    const code = decodeURIComponent(location.hash.slice(1)).trim();
    if (!codePattern.test(code)) {
      document.getElementById("without-code").hidden = false;
      return;
    }

    document.getElementById("with-code").hidden = false;
    document.getElementById("open").href = "vocroz://join?code=" + encodeURIComponent(code);
    document.getElementById("code").textContent = code;
    document.getElementById("copy").addEventListener("click", async () => {
      try {
        await navigator.clipboard.writeText(code);
        document.getElementById("copied").hidden = false;
      } catch {
        // クリップボードが使えない環境では、表示されたコードを手で選んでコピーしてもらう。
      }
    });
  });
})();
