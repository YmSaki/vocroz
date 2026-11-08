// SPDX-License-Identifier: AGPL-3.0-only
// テストの共通の準備。jsdom にない API をテスト用に補う。

import { afterEach } from "vitest";
import { cleanup } from "@testing-library/preact";

// jsdom は Canvas を実装していないので、メーターは描画せずに終わる（getContext が null を返す）。
// 何もしない getContext に差し替え、「未実装」の警告でテストの出力を埋めないようにする。
HTMLCanvasElement.prototype.getContext = (() =>
  null) as typeof HTMLCanvasElement.prototype.getContext;

afterEach(() => cleanup());
