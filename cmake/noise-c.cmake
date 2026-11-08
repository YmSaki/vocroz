# SPDX-License-Identifier: AGPL-3.0-only
#
# noise-c（Noise プロトコルの実装）を取得し、ターゲット noise::protocol を定義する。
# 使用してよい範囲は transport だけ（AGENTS.md 4.2、docs/adr/0001-key-exchange-noise-ik.md）。
#
# noise-c は autotools でしかビルドできないため、libnoiseprotocol の Makefile.am と同じソースを
# ここで CMake のターゲットにする。暗号プリミティブは libsodium のバックエンド（USE_LIBSODIUM）を使い、
# 鍵交換、AEAD、ハッシュの実装を libsodium にそろえる。
# noise-c にはリリースのタグがないため、コミットのハッシュで固定する。
if(NOT TARGET noise::protocol)
  include(${CMAKE_CURRENT_LIST_DIR}/libsodium.cmake)

  FetchContent_Declare(noise_c
    GIT_REPOSITORY https://github.com/rweather/noise-c.git
    GIT_TAG cfe25410979a87391bb9ac8d4d4bef64e9f268c6)
  # noise-c には CMakeLists.txt がないので、FetchContent はソースの取得だけを行う。
  FetchContent_MakeAvailable(noise_c)

  set(_noise_src "${noise_c_SOURCE_DIR}/src")
  add_library(vocroz_noise_protocol STATIC
    ${_noise_src}/protocol/cipherstate.c
    ${_noise_src}/protocol/dhstate.c
    ${_noise_src}/protocol/errors.c
    ${_noise_src}/protocol/handshakestate.c
    ${_noise_src}/protocol/hashstate.c
    ${_noise_src}/protocol/internal.c
    ${_noise_src}/protocol/names.c
    ${_noise_src}/protocol/patterns.c
    ${_noise_src}/protocol/randstate.c
    ${_noise_src}/protocol/signstate.c
    ${_noise_src}/protocol/symmetricstate.c
    ${_noise_src}/protocol/util.c
    ${_noise_src}/protocol/rand_sodium.c
    ${_noise_src}/backend/ref/hash-blake2s.c
    ${_noise_src}/backend/ref/cipher-aesgcm.c
    ${_noise_src}/backend/sodium/cipher-aesgcm.c
    ${_noise_src}/backend/sodium/cipher-chachapoly.c
    ${_noise_src}/backend/sodium/dh-curve25519.c
    ${_noise_src}/backend/sodium/hash-blake2b.c
    ${_noise_src}/backend/sodium/hash-sha256.c
    ${_noise_src}/backend/sodium/hash-sha512.c
    ${_noise_src}/backend/sodium/sign-ed25519.c
    ${_noise_src}/crypto/aes/rijndael-alg-fst.c
    ${_noise_src}/crypto/blake2/blake2s.c
    ${_noise_src}/crypto/ghash/ghash.c
    # Curve448 と NewHope の実装は GCC 拡張（__int128、ベクター型）を使い、MSVC でビルドできない。
    # VOCROZ は Curve25519 しか使わないので、両者は生成に失敗する代替の実装に差し替える。
    ${CMAKE_CURRENT_LIST_DIR}/noise-c/unsupported_dh.c)
  add_library(noise::protocol ALIAS vocroz_noise_protocol)
  target_include_directories(vocroz_noise_protocol
    SYSTEM PUBLIC "${noise_c_SOURCE_DIR}/include"
    PRIVATE "${_noise_src}" "${_noise_src}/protocol")
  target_compile_definitions(vocroz_noise_protocol PRIVATE USE_LIBSODIUM=1)
  if(MSVC)
    # noise-c は C の標準関数（strcpy など）を使っており、MSVC の非推奨警告を出さないようにする。
    target_compile_definitions(vocroz_noise_protocol PRIVATE _CRT_SECURE_NO_WARNINGS)
  endif()
  target_link_libraries(vocroz_noise_protocol PUBLIC sodium)
  # internal.c は USE_LIBSODIUM のときに libsodium の関数を呼ぶのに <sodium.h> を include していない。
  # 暗黙の関数宣言は新しい Clang と GCC でエラーになるため、ヘッダーを強制的に読み込ませる。
  if(MSVC)
    set(_noise_force_include /FIsodium.h)
  else()
    set(_noise_force_include -include;sodium.h)
  endif()
  set_source_files_properties(${_noise_src}/protocol/internal.c PROPERTIES
    COMPILE_OPTIONS "${_noise_force_include}")
  unset(_noise_force_include)
  unset(_noise_src)
endif()
