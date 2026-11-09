# SPDX-License-Identifier: AGPL-3.0-only
#
# network: UDP ソケット、ランデブーサーバーとの通信、UDP ホールパンチ、自動ポート開放（アダプター）。
# transport の DatagramSocket ポートを Winsock で実装し、1 本のソケットでランデブーサーバーと相手の両方と
# 通信する（docs/adr/0021-udp-socket-and-nat-traversal.md）。
# OS に依存しない部分（振り分け、ランデブーの手順、ホールパンチ、PCP と NAT-PMP の手順、パーサー）は
# どのプラットフォームでもビルドして単体テストし、Winsock と miniupnpc を使う src/win/ は Windows でだけビルドする。
add_library(vocroz_network STATIC)
target_sources(vocroz_network PRIVATE
  src/address_selection.cpp
  src/chained_port_mapper.cpp
  src/client_rendezvous.cpp
  src/hole_puncher.cpp
  src/host_rendezvous.cpp
  src/https_rendezvous_client.cpp
  src/pcp_port_mapper.cpp
  src/peer_datagram_socket.cpp
  src/receive_router.cpp
  src/rendezvous_registration.cpp
  src/rendezvous_url.cpp
  src/wire/base64url.cpp
  src/wire/invite_api_codec.cpp
  src/wire/nat_pmp_codec.cpp
  src/wire/pcp_codec.cpp
  src/wire/rendezvous_codec.cpp)
if(WIN32)
  target_sources(vocroz_network PRIVATE
    src/win/factories.cpp
    src/win/miniupnpc_port_mapper.cpp
    src/win/win_network_environment.cpp
    src/win/winhttp_https_transport.cpp
    src/win/winsock_address.cpp
    src/win/winsock_gateway_channel.cpp
    src/win/winsock_runtime.cpp
    src/win/winsock_udp_endpoint.cpp)
endif()
add_library(vocroz::network ALIAS vocroz_network)
target_include_directories(vocroz_network
  PUBLIC include
  PRIVATE src)
target_link_libraries(vocroz_network PUBLIC vocroz::common vocroz::transport vocroz::session)
# nlohmann/json は招待 API の JSON（src/wire/invite_api_codec.cpp）だけに使い、公開ヘッダーに型を出さない。
target_link_libraries(vocroz_network PRIVATE nlohmann_json::nlohmann_json)
if(WIN32)
  # miniupnpc は UPnP-IGD のポート開放だけに使い、公開ヘッダーに型を出さない（AGENTS.md 4.2）。
  target_link_libraries(vocroz_network PRIVATE ws2_32 iphlpapi winhttp miniupnpc::miniupnpc)
  # windows.h の min / max マクロが std::numeric_limits<T>::max() などを壊さないようにする。
  target_compile_definitions(vocroz_network PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()
vocroz_set_warnings(vocroz_network)

if(VOCROZ_BUILD_TESTS)
  vocroz_add_unit_test(network_tests
    tests/address_selection_test.cpp
    tests/base64url_test.cpp
    tests/chained_port_mapper_test.cpp
    tests/client_rendezvous_test.cpp
    tests/hole_puncher_test.cpp
    tests/host_rendezvous_test.cpp
    tests/https_rendezvous_client_test.cpp
    tests/invite_api_codec_test.cpp
    tests/nat_pmp_codec_test.cpp
    tests/pcp_codec_test.cpp
    tests/pcp_port_mapper_test.cpp
    tests/peer_datagram_socket_test.cpp
    tests/receive_router_test.cpp
    tests/rendezvous_codec_test.cpp
    tests/rendezvous_registration_test.cpp
    tests/rendezvous_url_test.cpp)
  if(WIN32)
    # ループバックで実際に送受信する。Windows の CI で Winsock のアダプターを確かめる。
    target_sources(network_tests PRIVATE
      tests/win_network_environment_test.cpp
      tests/winhttp_https_transport_test.cpp
      tests/winsock_udp_endpoint_test.cpp)
  endif()
  # src/ のクラス（HolePuncher など）は公開せず、同じモジュールのテストからだけ参照する。
  # tests/ はこのモジュールのフェイク（tests/fakes/）を参照するため。
  target_include_directories(network_tests PRIVATE src tests)
  target_link_libraries(network_tests PRIVATE vocroz::network vocroz::common_fakes vocroz::transport_fakes
    nlohmann_json::nlohmann_json)
endif()
