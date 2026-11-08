// SPDX-License-Identifier: AGPL-3.0-only
//
// ログを出力するポート。
// ログの出力先（ファイルなど）は persistence が実装し、ドメインはこのポート経由でだけログを出す
// （AGENTS.md 6.4）。テストでは FakeLogger に差し替えて、出力された内容を検証する。
#pragma once

#include <string_view>

namespace vocroz::common {

/// ログの重要度。
enum class LogLevel {
    kDebug,    ///< 開発時の調査用
    kInfo,     ///< 通常の動作の記録（接続、承認など）
    kWarning,  ///< 動作は継続できるが、注意が必要な事象
    kError,    ///< 処理を継続できなかった事象
};

/// ログの出力先。
/// メッセージに PCM データ、招待トークン、鍵、個人情報を含めない（AGENTS.md 6.4）。
/// 実装はコンポジションルートで生成し、参照で注入する（AGENTS.md 4.3）。
class Logger {
public:
    virtual ~Logger() = default;

    /// ログを 1 件出力する。
    /// @param level   重要度
    /// @param message メッセージ。呼び出しから戻った後は参照されない
    /// 出力に失敗しても呼び出し側には通知しない（ログの失敗で本来の処理を止めないため）。
    /// スレッド安全性: オーディオスレッド以外のどのスレッドからも同時に呼び出し可能。
    /// 実装は I/O やロックを含みうるので、オーディオスレッドからは呼び出さない（AGENTS.md 第 9
    /// 節）。
    virtual void log(LogLevel level, std::string_view message) noexcept = 0;
};

}  // namespace vocroz::common
