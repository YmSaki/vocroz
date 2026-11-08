// SPDX-License-Identifier: AGPL-3.0-only
//
// パイプのアクセス制御に使う、ログイン中ユーザーの SID とセキュリティ記述子。
// ACL の形となりすまし対策の理由は docs/adr/0020-ipc-pipe-and-webui-bridge.md。
#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <windows.h>

namespace vocroz::ipc::win {

/// このプロセスのトークンのユーザーの SID を文字列（`S-1-5-21-...`）で返す。
/// @return SID。トークンを読めなければ std::nullopt
[[nodiscard]] std::optional<std::wstring> current_user_sid();

/// 所有者と、ただ 1 つの許可の ACE を指定した SID にしたセキュリティ記述子。
/// SDDL は `O:<SID>D:P(A;;GA;;;<SID>)`。継承を受けないので、ほかのユーザーと Administrators
/// も含めてアクセスできない。
class OwnerOnlySecurity {
public:
    /// @param sid 所有者と許可の対象にする SID の文字列
    /// @return セキュリティ記述子。SID の形式が不正なら std::nullopt
    [[nodiscard]] static std::optional<OwnerOnlySecurity> create(std::wstring_view sid);

    ~OwnerOnlySecurity();
    OwnerOnlySecurity(const OwnerOnlySecurity&) = delete;
    OwnerOnlySecurity& operator=(const OwnerOnlySecurity&) = delete;
    OwnerOnlySecurity(OwnerOnlySecurity&& other) noexcept;
    OwnerOnlySecurity& operator=(OwnerOnlySecurity&& other) noexcept;

    /// CreateNamedPipeW に渡す SECURITY_ATTRIBUTES。このオブジェクトより長く使わない。
    [[nodiscard]] SECURITY_ATTRIBUTES* attributes() noexcept {
        return &attributes_;
    }

private:
    explicit OwnerOnlySecurity(PSECURITY_DESCRIPTOR descriptor) noexcept;

    PSECURITY_DESCRIPTOR descriptor_ = nullptr;
    SECURITY_ATTRIBUTES attributes_{};
};

/// パイプ（カーネルオブジェクト）の所有者の SID を文字列で返す。
/// @param pipe READ_CONTROL の権限で開いたパイプ
/// @return 所有者の SID。読めなければ std::nullopt
[[nodiscard]] std::optional<std::wstring> owner_sid_of(HANDLE pipe);

}  // namespace vocroz::ipc::win
