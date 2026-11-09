// SPDX-License-Identifier: AGPL-3.0-only
//
// 親機の出力デバイスの一覧、開く、閉じるを行うポート。
// オペレーターの選択（SelectOutput）と、起動時に保存した出力デバイスを開く処理で使う。
// コンポジションルートが device の AudioDeviceCatalog と AudioOutput で実装する。
#pragma once

#include <string>
#include <vector>

#include "vocroz/common/result.h"
#include "vocroz/control_protocol/entities.h"
#include "vocroz/control_protocol/states.h"

namespace vocroz::session {

/// 親機の出力デバイスの操作。開いている出力デバイスは同時に 1 つだけ。
/// スレッド安全性: コントロールスレッドから呼ぶ。
class OutputDeviceControl {
public:
    virtual ~OutputDeviceControl() = default;

    /// 今使える出力デバイスを列挙する。
    /// @return デバイスの一覧。取得できなければ空
    [[nodiscard]] virtual std::vector<control_protocol::OutputDevice> list_outputs() = 0;

    /// 指定したデバイスを開き、出力を始める。開いているデバイスがあれば、先に close_output()
    /// で閉じてから呼ぶ（ミキサーを 2 つのオーディオスレッドから呼ばないため）。
    /// @param device_id デバイスの永続 ID
    /// @return 開いたデバイスの表示名。開けなければ DEVICE_UNAVAILABLE
    /// エラー時の振る舞い: 別のデバイスへフォールバックしない。何も開かないまま返す。
    [[nodiscard]] virtual common::Result<std::string, control_protocol::ErrorCode> open_output(
        const std::string& device_id) = 0;

    /// 開いているデバイスの出力を止めて閉じる。開いていなければ何もしない。
    virtual void close_output() = 0;
};

}  // namespace vocroz::session
