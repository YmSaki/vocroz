# common

全モジュールとアプリが使う共通基盤です。レイヤーは共通基盤で、どのモジュールにも依存しません（AGENTS.md 4.2）。

## 責務

- ステートレスなユーティリティ（コマンドライン引数、バージョン表示）
- 失敗しうる処理の戻り値に使う Result 型
- オーディオスレッドへのパラメーター受け渡しに使うロックフリー SPSC キュー
- 横断的関心事のポート（`Clock`、`Logger`）

特定モジュールのドメイン知識は持ちません。

## 公開 API

| ヘッダー | 内容 | オーディオスレッド |
| --- | --- | --- |
| `vocroz/common/cli.h` | `make_args()`、`has_flag()` | 対象外 |
| `vocroz/common/version_info.h` | `version_line()` | 対象外 |
| `vocroz/common/result.h` | `Result<T, E>`、`Result<void, E>`、`make_unexpected()`。C++23 の `std::expected` と同じ形の API | T と E がヒープを確保しなければ使える |
| `vocroz/common/spsc_queue.h` | `SpscQueue<T, Capacity>`。固定長、ヒープ確保なし、`try_push()` と `try_pop()` は `noexcept` | 呼び出し可能 |
| `vocroz/common/ports/clock.h` | `Clock`（単調増加する時刻） | 実装による（ブロックしない） |
| `vocroz/common/ports/logger.h` | `Logger`、`LogLevel` | 呼び出さない |

### Result 型の使い方

エラーの型は、各モジュールが原因別の `enum class` として定義します（AGENTS.md 6.2）。

```cpp
enum class InviteError { kMalformed, kExpired };

vocroz::common::Result<InviteCode, InviteError> parse_invite(std::string_view text) {
    if (text.empty()) {
        return vocroz::common::make_unexpected(InviteError::kMalformed);
    }
    return InviteCode{text};
}
```

## テストダブル

`tests/fakes/` の `FakeClock` と `FakeLogger` は、CMake ターゲット `vocroz::common_fakes` で他モジュールのテストからも使えます。

```cmake
target_link_libraries(session_tests PRIVATE vocroz::session vocroz::common_fakes)
```

```cpp
#include "fakes/fake_clock.h"
```

## 依存してよいモジュール

なし。
