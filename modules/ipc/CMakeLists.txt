# SPDX-License-Identifier: AGPL-3.0-only
#
# ipc: 名前付きパイプのサーバーとクライアント（MessageChannel の実装）と、その上のフレーミング。
# フレーミングとパイプの名前の導出は OS に依存しないので、どのプラットフォームでもビルドしてテストする。
# パイプの実装（src/win/）は Windows だけ（docs/adr/0020-ipc-pipe-and-webui-bridge.md）。
add_library(vocroz_ipc STATIC
  src/frame_codec.cpp
  src/pipe_name.cpp)
add_library(vocroz::ipc ALIAS vocroz_ipc)
target_include_directories(vocroz_ipc
  PUBLIC include
  PRIVATE src)
target_link_libraries(vocroz_ipc
  PUBLIC vocroz::control_protocol vocroz::common)
if(WIN32)
  target_sources(vocroz_ipc PRIVATE
    src/win/named_pipe_client.cpp
    src/win/named_pipe_factories.cpp
    src/win/named_pipe_server.cpp
    src/win/named_pipe_stream.cpp
    src/win/win32_security.cpp
    src/win/win32_text.cpp)
  target_compile_definitions(vocroz_ipc PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
  target_link_libraries(vocroz_ipc PRIVATE advapi32)
endif()
vocroz_set_warnings(vocroz_ipc)

if(VOCROZ_BUILD_TESTS)
  vocroz_add_unit_test(ipc_tests
    tests/frame_codec_test.cpp
    tests/pipe_name_test.cpp)
  target_link_libraries(ipc_tests PRIVATE vocroz::ipc)
  if(WIN32)
    target_sources(ipc_tests PRIVATE tests/named_pipe_test.cpp)
    target_compile_definitions(ipc_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    target_link_libraries(ipc_tests PRIVATE vocroz::common_fakes advapi32)
  endif()
endif()
