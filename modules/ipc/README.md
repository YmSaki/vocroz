# ipc

`remote-pa`（親機モード）と `pa-engine` をつなぐ名前付きパイプのアダプターです。レイヤーはアダプターで、依存してよいのは `control-protocol` と `common` だけです（AGENTS.md 4.2）。
フレーミング、ACL、なりすまし対策、切断と再接続の扱いは `docs/adr/0020-ipc-pipe-and-webui-bridge.md` で決めています。

## 責務

- 名前付きパイプのサーバー（`pa-engine`）とクライアント（`remote-pa`）。どちらも `control_protocol::MessageChannel` を実装する
- パイプの ACL（所有者と許可をログイン中ユーザーの SID だけにする）と、クライアント側での所有者の確認
- 長さ付き JSON のフレーミング（4 バイトのリトルエンディアンの長さ + 本文）
- パイプの名前の導出（ユーザーの SID と `--data-dir`）

メッセージの検証とコマンドの適用は持ちません（`control-protocol`、`session`）。

## 公開 API

| ヘッダー | 内容 |
| --- | --- |
| `vocroz/ipc/factories.h` | `make_pipe_server()`、`connect_pipe_client()`、`engine_pipe_name()`、`PipeError`（Windows だけ） |
| `vocroz/ipc/frame_codec.h` | `encode_frame()`、`FrameDecoder`、`FrameError` |
| `vocroz/ipc/pipe_name.h` | `make_engine_pipe_name()`（名前の規則。OS に依存しない） |

### 振る舞い

- サーバーの `MessageChannel` は、UI がつながり直しても同じオブジェクトです。UI が切断したら次の接続を待ちます。UI の切断は `on_closed()` で知らせません。
- クライアントの `MessageChannel` は 1 つの接続です。切断されたら `on_closed()` を 1 回呼びます。つながり直すときは `connect_pipe_client()` で作り直します。
- `send()` は送信キューに積んで戻ります。未送信が 1 MiB を超えたら、相手がハングしたとみなして接続を切ります。
- 受信は経路ごとの受信スレッドでリスナーに渡します。

### pa-engine での使い方

```cpp
auto name = ipc::engine_pipe_name(options.data_dir);
auto server = ipc::make_pipe_server(name.value(), logger);
if (!server) {
    // kAlreadyExists: 同じエンジンが動いているか、名前を先取りされている。起動を止める。
}
server.value()->set_listener(&session_listener);
```

## テスト

- `frame_codec_test.cpp`、`pipe_name_test.cpp`: OS に依存しないので、Linux の CI でも動きます。
- `named_pipe_test.cpp`: Windows だけ。実際のパイプで往復、再接続、ACL の形、不正なフレーム、送信キューのあふれを確かめます。

## 依存してよいモジュール

`control-protocol`、`common`。Win32 の API（advapi32）は `src/win/` の中だけで使います。
