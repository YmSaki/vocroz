// SPDX-License-Identifier: AGPL-3.0-only
//
// ログイン中ユーザーの SID とセキュリティ記述子。advapi32 の API を使う。

#include "win/win32_security.h"

#include <utility>
#include <vector>

#include <aclapi.h>
#include <sddl.h>

#include "win/win32_handle.h"

namespace vocroz::ipc::win {
namespace {

// LocalAlloc で確保されたメモリ（ConvertSidToStringSidW などの戻り値）を LocalFree で解放する。
struct LocalFreeDeleter {
    void operator()(void* memory) const noexcept {
        LocalFree(memory);
    }
};

std::optional<std::wstring> sid_to_string(PSID sid) {
    LPWSTR text = nullptr;
    if (ConvertSidToStringSidW(sid, &text) == FALSE) {
        return std::nullopt;
    }
    std::wstring result{text};
    LocalFreeDeleter{}(text);
    return result;
}

}  // namespace

std::optional<std::wstring> current_user_sid() {
    HANDLE raw_token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw_token) == FALSE) {
        return std::nullopt;
    }
    const UniqueHandle token{raw_token};

    DWORD size = 0;
    GetTokenInformation(token.get(), TokenUser, nullptr, 0, &size);
    if (size == 0) {
        return std::nullopt;
    }
    // TOKEN_USER の後ろに SID の本体が続くので、DWORD 単位で確保して境界をそろえる。
    std::vector<DWORD> buffer((size + sizeof(DWORD) - 1) / sizeof(DWORD));
    if (GetTokenInformation(token.get(), TokenUser, buffer.data(), size, &size) == FALSE) {
        return std::nullopt;
    }
    const auto* user = reinterpret_cast<const TOKEN_USER*>(buffer.data());
    return sid_to_string(user->User.Sid);
}

std::optional<OwnerOnlySecurity> OwnerOnlySecurity::create(std::wstring_view sid) {
    const std::wstring sid_text{sid};
    // O: 所有者、D:P 継承を受けない DACL、(A;;GA;;;SID) その SID だけにすべての権限を許可する。
    const std::wstring sddl = L"O:" + sid_text + L"D:P(A;;GA;;;" + sid_text + L")";
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1,
                                                             &descriptor, nullptr) == FALSE) {
        return std::nullopt;
    }
    return OwnerOnlySecurity{descriptor};
}

OwnerOnlySecurity::OwnerOnlySecurity(PSECURITY_DESCRIPTOR descriptor) noexcept
    : descriptor_(descriptor) {
    attributes_.nLength = sizeof(attributes_);
    attributes_.lpSecurityDescriptor = descriptor_;
    attributes_.bInheritHandle = FALSE;
}

OwnerOnlySecurity::~OwnerOnlySecurity() {
    if (descriptor_ != nullptr) {
        LocalFreeDeleter{}(descriptor_);
    }
}

OwnerOnlySecurity::OwnerOnlySecurity(OwnerOnlySecurity&& other) noexcept
    : descriptor_(std::exchange(other.descriptor_, nullptr)), attributes_(other.attributes_) {
    other.attributes_.lpSecurityDescriptor = nullptr;
}

OwnerOnlySecurity& OwnerOnlySecurity::operator=(OwnerOnlySecurity&& other) noexcept {
    if (this != &other) {
        if (descriptor_ != nullptr) {
            LocalFreeDeleter{}(descriptor_);
        }
        descriptor_ = std::exchange(other.descriptor_, nullptr);
        attributes_ = other.attributes_;
        other.attributes_.lpSecurityDescriptor = nullptr;
    }
    return *this;
}

std::optional<std::wstring> owner_sid_of(HANDLE pipe) {
    PSID owner = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (GetSecurityInfo(pipe, SE_KERNEL_OBJECT, OWNER_SECURITY_INFORMATION, &owner, nullptr,
                        nullptr, nullptr, &descriptor) != ERROR_SUCCESS) {
        return std::nullopt;
    }
    // owner は descriptor の中を指すので、文字列にしてから解放する。
    auto result = sid_to_string(owner);
    LocalFreeDeleter{}(descriptor);
    return result;
}

}  // namespace vocroz::ipc::win
