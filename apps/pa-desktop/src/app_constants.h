// SPDX-License-Identifier: AGPL-3.0-only
//
// remote-pa の組み立てだけで使う定数。値の根拠はそれぞれのコメントに書く。
#pragma once

#include <chrono>

namespace vocroz::pa_desktop {

/// pa-engine が「同じデータフォルダのエンジンがすでに動いている」ときに返す終了コード
/// （docs/adr/0007-launch-interface.md、docs/adr/0028-engine-assembly.md）。
inline constexpr int kEngineExitAlreadyRunning = 3;

/// エンジンを起動してから、パイプにつながるまで待つ時間。エンジンの起動はデバイスの列挙と
/// 鍵の生成で数百ミリ秒かかる。遅い PC やウイルス対策の検査でも収まるよう、長めにとる。
inline constexpr std::chrono::milliseconds kEngineStartTimeout{10000};

/// パイプへの接続を試し直す間隔。起動を待つ間にメッセージスレッドを占有しないよう、
/// 1 回の接続は短く待ち（kEngineConnectWait）、間を空けて試す。
inline constexpr std::chrono::milliseconds kEngineConnectRetryInterval{100};

/// 1 回の接続で、ビジーなパイプが空くのを待つ時間。メッセージスレッドで待つので短くする。
inline constexpr std::chrono::milliseconds kEngineConnectWait{50};

/// エンジンへの接続の手順を進める間隔。kEngineConnectRetryInterval より短くして、
/// 試し直しの間隔が手順の周期で延びないようにする。
inline constexpr std::chrono::milliseconds kAttachTickInterval{20};

/// 親機の画面の大きさ。操作卓として 8 本ほどのストリップが並ぶ大きさにし、最小は
/// ストリップ 4 本とヘッダーが収まる大きさ（docs/adr/0005-ui-juce-webview.md）。
inline constexpr int kHostWindowWidth = 1280;
inline constexpr int kHostWindowHeight = 800;
inline constexpr int kHostWindowMinWidth = 960;
inline constexpr int kHostWindowMinHeight = 600;

/// 親機モードの WebView2 のデータを置く、データフォルダの下のフォルダ名。
/// 同じデータフォルダで子機モードと同時に動かしても、ブラウザープロセスを共有しないよう分ける。
inline constexpr const char* kHostWebViewDataFolderName = "webview2-host";

/// ログファイルの名前の基になる名前。モードごとに分け、同じデータフォルダで親機モードと
/// 子機モードを同時に動かしても、1 つのファイルを奪い合わないようにする。
inline constexpr const char* kHostLogBaseName = "remote-pa-host";
inline constexpr const char* kClientLogBaseName = "remote-pa-client";

}  // namespace vocroz::pa_desktop
