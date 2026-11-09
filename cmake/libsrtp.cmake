# SPDX-License-Identifier: AGPL-3.0-only
#
# libsrtp を取得し、ターゲット libSRTP::srtp2 を定義する。使用してよい範囲は transport だけ
# （AGENTS.md 4.2、docs/adr/0040-srtp-external-output.md）。
# 暗号のバックエンドは libsrtp の内蔵の実装を使い、OpenSSL などには依存しない（ENABLE_OPENSSL などは既定の OFF のまま）。
# バージョンはタグ v2.7.0 のコミットで固定する（docs/adr/0010-external-libraries.md）。
if(NOT TARGET libSRTP::srtp2)
  # libsrtp のテスト用の実行ファイルはビルドしない。
  set(LIBSRTP_TEST_APPS OFF CACHE BOOL "" FORCE)
  # libsrtp の既定は警告をエラーにする設定で、コンパイラの版によってはビルドが止まるので外す。
  set(ENABLE_WARNINGS OFF CACHE BOOL "" FORCE)
  set(ENABLE_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)

  FetchContent_Declare(libsrtp
    GIT_REPOSITORY https://github.com/cisco/libsrtp.git
    GIT_TAG ee1a77c9f9dc02c42bda9901038c500c5efe4cfa  # v2.7.0
    SYSTEM)  # 外部ヘッダーの警告を出さない
  FetchContent_MakeAvailable(libsrtp)
endif()
