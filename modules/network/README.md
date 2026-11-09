# network

UDP ソケット、ランデブーサーバーとの通信（UDP と、招待 API の HTTPS）、UDP ホールパンチ、自動ポート開放を担うアダプターのモジュールです。
依存してよいのは `transport`、`session`、`common` です（AGENTS.md 4.2）。`session` は招待 API の `RendezvousClient` ポートを実装するために使います。
ソケットの持ち方、NAT 越えの手順、ポート開放の方法は [ADR-0021](../../docs/adr/0021-udp-socket-and-nat-traversal.md) で決めています。
ランデブーメッセージの形式の正本は `infra/rendezvous/internal/udpapi/wire.go`、招待 API の JSON の正本は `infra/rendezvous/internal/httpapi/invites.go`（AGENTS.md 4.4）です。

## 責務

- Winsock のデュアルスタックの UDP ソケット 1 本と、ネットワーク受信スレッド（`src/win/`）
- 受信したデータグラムの振り分け（ランデブーメッセージはコントロールスレッドへ、相手のパケットは受信スレッドのまま transport へ）
- transport の `DatagramSocket` ポートの実装（`PeerDatagramSocket`）
- ランデブーサーバーへの登録（REGISTER の送り直し、REGISTERED と PUNCH の検証）とホールパンチ
- 子機の経路の確立の打ち切りと、原因別のエラー（`kRendezvousUnreachable`、`kRendezvousCapacity`、`kHostNotFound`、`kUnreachable`）
- PCP、NAT-PMP（自前の実装）、UPnP-IGD（miniupnpc）による自動ポート開放
- ローカル候補アドレスとランデブーサーバーのアドレスの選択
- session の `RendezvousClient`（招待の登録、解決、失効）の HTTPS による実装。JSON の組み立てと応答の検証（`src/wire/`）、WinHTTP のアダプター（`src/win/`）

パケットのパースと暗号化（`transport`）、接続状態の遷移と再接続の判断（`session`）は持ちません。

## 公開 API

| ヘッダー | 内容 | 呼ぶスレッド |
| --- | --- | --- |
| `vocroz/network/udp_address.h` | `UdpAddress`（IPv4 は IPv4 射影アドレスで持つ） | — |
| `vocroz/network/network_error.h` | `NetworkError`（失敗の原因） | — |
| `vocroz/network/rendezvous_types.h` | `RendezvousId`、`PeerId`、`RendezvousRole`、`kMaxLocalCandidates` | — |
| `vocroz/network/address_selection.h` | `select_local_candidates()`、`select_server_address()`、`is_routable_candidate()` | どこからでも |
| `vocroz/network/receive_router.h` | `ReceiveRouter`（受信の振り分け）、`RendezvousDatagram` | 受信はネットワーク受信、取り出しはコントロール |
| `vocroz/network/peer_datagram_socket.h` | `PeerDatagramSocket`（`transport::DatagramSocket` の実装。送信先の更新は SPSC キュー） | 送信は 1 つの送信スレッド、更新はネットワーク受信 |
| `vocroz/network/client_rendezvous.h` | `ClientRendezvous`（子機の登録、ホールパンチ、打ち切り） | コントロール |
| `vocroz/network/host_rendezvous.h` | `HostRendezvous`（親機の登録、ホールパンチ、登録の状態の通知） | コントロール |
| `vocroz/network/rendezvous_url.h` | `parse_rendezvous_url()`（ランデブーサーバーの URL の検証） | どこからでも |
| `vocroz/network/https_rendezvous_client.h` | `HttpsRendezvousClient`（`session::RendezvousClient` の実装。要求を作業用のスレッドで順に送る） | 要求はコントロール、結果は作業用のスレッドから |
| `vocroz/network/factories.h` | `make_winsock_udp_endpoint()`、`make_win_network_environment()`、`make_port_mapper()`、`make_winhttp_https_transport()`（Windows だけ） | 各関数の説明のとおり |
| `vocroz/network/ports/` | `UdpEndpoint`、`PortMapper`、`GatewayChannel`、`NetworkEnvironment`、`HttpsTransport`（ポート）、`DatagramListener`、`ClientRendezvousListener`、`HostRendezvousListener`、`InviteResultListener`（リスナー） | 各ヘッダーの説明のとおり |

### 組み立ての例（子機）

```cpp
using namespace vocroz::network;

// ネットワーク受信スレッド: 相手のパケットは transport の受信処理（PacketOpener など）へ。
ReceiveRouter router(peer_packet_listener);
auto endpoint = make_winsock_udp_endpoint(0, router).value();

// コントロールスレッド: サーバーのアドレスと候補を選び、鍵交換の 1 つ目のメッセージを全候補へ送る。
auto env = make_win_network_environment();
ClientRendezvousRequest request;
request.server = *select_server_address(env->resolve("rendezvous.example", 3478).value());
request.rendezvous_id = resolved_invite_rendezvous_id;
request.local_candidates = select_local_candidates(env->interface_addresses(), endpoint->local_port(), std::nullopt);
request.opening_datagram = handshake_initiation;
ClientRendezvous rendezvous(*endpoint, random, failure_listener);
rendezvous.start(request, clock.now());

// コントロールスレッドのループ: キューのランデブーメッセージを渡し、next_deadline() に poll() する。
while (auto message = router.pop_rendezvous()) {
    rendezvous.on_rendezvous_datagram(*message, clock.now());
}
rendezvous.poll(clock.now());

// 認証済みの応答を返した送信元を送信先にする。
PeerDatagramSocket socket(*endpoint, response_source);
rendezvous.mark_established();
```

親機は `make_port_mapper()` の `map()` を作業用のスレッドで呼び、得た外部アドレスを `select_local_candidates()` の `mapped` に渡してから `HostRendezvous::start()` を呼びます。寿命の半分で `map()` を呼び直し、セッションの終了時に `unmap()` を呼びます。

### 組み立ての例（招待 API）

```cpp
// 親機と子機で共通。URL は https のベース URL（開発用にループバックの http も可）。
auto https = make_winhttp_https_transport("https://rendezvous.example").value();
// listener は InviteResultListener を実装し、結果をコントロールスレッドへ渡してから
// HostSession::on_invite_registered() か ClientSession::on_invite_resolved() を呼ぶ。
HttpsRendezvousClient rendezvous_client(*https, listener);
// session には session::RendezvousClient として注入する。
```

`HttpsRendezvousClient` を破棄すると、キューに残った要求（終了時の失効など）を送り終えるまで待ちます。`HttpsTransport` と `listener` より先に破棄してください。

## 組み立て側（E1、子機）への約束

- 親機は同じ鍵交換のメッセージを複数の経路から受け取ることがあります。接続 ID が同じ 2 つ目以降のメッセージは重複として扱ってください。
- 親機が受け取った認証済みのパケットの送信元は `HostRendezvous::mark_established()` に渡し、送信先が変わったら `PeerDatagramSocket::update_remote()` に渡します。
- `NetworkError` から `control_protocol::ErrorCode` への変換は組み立て側で行います（`kUnreachable` などは `UNREACHABLE` になります）。

## テスト

OS に依存しない部分は Linux と Windows の両方で単体テストします（`tests/`）。
Winsock のソケットと WinHTTP は、ループバックで実際に送受信するテスト（`winsock_udp_endpoint_test.cpp`、`winhttp_https_transport_test.cpp`）を Windows でだけ実行します。WinHTTP のテストはループバックの http で行い、TLS の検証は OS に任せています。
ランデブーメッセージの期待値は、サーバーの実装が生成したバイト列をそのまま使い、形式が一致することを確かめています。
