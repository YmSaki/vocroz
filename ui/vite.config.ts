// SPDX-License-Identifier: AGPL-3.0-only
// UI のビルドとテストの設定。
// JSX は esbuild で Preact に変換し、Babel のプラグインに依存しない（依存を最小限にするため）。
// 成果物（dist/）は remote-pa にバイナリリソースとして埋め込み、JUCE のリソースプロバイダーで配信する。
/// <reference types="vitest/config" />
import { defineConfig } from "vite";

export default defineConfig({
  // リソースプロバイダーはルートからの相対パスで配信するので、絶対パスを埋め込まない。
  base: "./",
  esbuild: {
    jsx: "automatic",
    jsxImportSource: "preact",
  },
  build: {
    outDir: "dist",
    emptyOutDir: true,
    // WebView2 は Chromium ベースなので、新しい構文をそのまま出力してよい。
    target: "es2022",
  },
  test: {
    environment: "jsdom",
    setupFiles: ["./src/test-setup.ts"],
    include: ["src/**/*.test.{ts,tsx}"],
  },
});
