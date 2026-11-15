// SPDX-License-Identifier: AGPL-3.0-only
//
// 録音の書き出しで、ボリュームの空き容量が下限を下回る前に止めるための判断
// （docs/adr/0043-recording-and-media-player.md）。
// 空き容量の取得（OS や JUCE の呼び出し）は関数で受け取り、ここは「いつ調べて、書いてよいか」だけを
// 決める。JUCE を使わずに Linux の CI でテストするため。
#pragma once

#include <cstdint>
#include <functional>
#include <optional>

namespace vocroz::media::detail {

/// 書き出しのたびに、書いてよいかを決める。
/// スレッド安全性: 1 つのスレッド（ディスク I/O スレッド）からだけ使う。
class FreeSpaceGuard {
public:
    /// ボリュームの空きのバイト数を返す関数。調べられなければ std::nullopt。
    using FreeBytesQuery = std::function<std::optional<std::uint64_t>()>;

    /// @param query          空き容量を返す関数（空なら常に書いてよいとする）
    /// @param min_free_bytes 書き出しの後に残す空きの下限
    /// @param check_interval 空きを調べる間隔（書き出したバイト数）
    FreeSpaceGuard(FreeBytesQuery query, std::uint64_t min_free_bytes,
                   std::uint64_t check_interval);

    /// これから bytes バイトを書いてよいかを返す。最初の呼び出しと、前に調べてから
    /// check_interval バイトを書くたびに空きを調べる。
    /// 調べた空きから、その後に書いた量と bytes を引いて下限を下回るなら false。
    /// 空きを調べられなければ書いてよいとする（調べられないことで録音を止めない）。
    /// @param bytes これから書くバイト数
    [[nodiscard]] bool admit(std::uint64_t bytes);

    /// 今すぐ空きを調べ、下限を下回っていれば true を返す。書き込みが失敗したときに、
    /// 容量不足かそれ以外かを分けるのに使う。調べられなければ false。
    [[nodiscard]] bool is_below_minimum();

private:
    FreeBytesQuery query_;
    std::uint64_t min_free_bytes_;
    std::uint64_t check_interval_;
    // 最後に調べた空き。std::nullopt はまだ調べていないか、調べられなかった。
    std::optional<std::uint64_t> last_free_;
    // 最後に調べてから書いたバイト数。
    std::uint64_t written_since_check_ = 0;
    bool checked_ = false;
};

}  // namespace vocroz::media::detail
