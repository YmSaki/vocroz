// SPDX-License-Identifier: AGPL-3.0-only
//
// wintrust_code_signature_verifier.h の実装。

#include "win/wintrust_code_signature_verifier.h"

// windows.h より後に include する必要がある。
// clang-format off
#include <windows.h>
#include <softpub.h>
#include <wincrypt.h>
#include <wintrust.h>
// clang-format on

#include <algorithm>
#include <string>
#include <system_error>

#include "vocroz/update/install_pointer.h"

#include "updater_constants.h"
#include "win/win32_text.h"

namespace vocroz::updater::win {
namespace {

namespace fs = std::filesystem;

[[nodiscard]] bool is_signable(const fs::path& path) {
    std::wstring extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), ::towlower);
    return extension == L".exe" || extension == L".dll";
}

// バージョン別フォルダに入る、自分たちがビルドした実行ファイルか。
[[nodiscard]] bool is_own_executable(const fs::path& path) {
    for (const std::string_view name : kVersionedExecutables) {
        if (path.filename() == fs::path{widen(name) + L".exe"}) {
            return true;
        }
    }
    return false;
}

// 1 つのファイルの署名を検証し、署名者の証明書の名前を返す。信頼できなければ空。
[[nodiscard]] std::wstring trusted_signer_name(const fs::path& file) {
    WINTRUST_FILE_INFO file_info{};
    file_info.cbStruct = sizeof(file_info);
    file_info.pcwszFilePath = file.c_str();

    WINTRUST_DATA data{};
    data.cbStruct = sizeof(data);
    data.dwUIChoice = WTD_UI_NONE;
    // 失効の確認は証明書チェーン全体で行う。オフラインでは失敗するが、更新はオンラインでしか
    // 届かないので、確認できない署名を通さない側に倒す。
    data.fdwRevocationChecks = WTD_REVOKE_WHOLECHAIN;
    data.dwUnionChoice = WTD_CHOICE_FILE;
    data.pFile = &file_info;
    data.dwStateAction = WTD_STATEACTION_VERIFY;
    data.dwProvFlags = WTD_REVOCATION_CHECK_CHAIN;

    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const LONG status = ::WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &data);
    std::wstring name;
    if (status == ERROR_SUCCESS) {
        CRYPT_PROVIDER_DATA* provider = ::WTHelperProvDataFromStateData(data.hWVTStateData);
        CRYPT_PROVIDER_SGNR* signer =
            provider != nullptr ? ::WTHelperGetProvSignerFromChain(provider, 0, FALSE, 0) : nullptr;
        CRYPT_PROVIDER_CERT* cert =
            signer != nullptr ? ::WTHelperGetProvCertFromChain(signer, 0) : nullptr;
        if (cert != nullptr && cert->pCert != nullptr) {
            wchar_t buffer[256]{};
            const DWORD length = ::CertGetNameStringW(cert->pCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0,
                                                      nullptr, buffer, 256);
            if (length > 1) {
                name.assign(buffer, length - 1);
            }
        }
    }
    data.dwStateAction = WTD_STATEACTION_CLOSE;
    ::WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &data);
    return name;
}

}  // namespace

WinTrustCodeSignatureVerifier::WinTrustCodeSignatureVerifier(fs::path install_dir)
    : install_dir_(std::move(install_dir)) {}

bool WinTrustCodeSignatureVerifier::verify(const update::SemanticVersion& version,
                                           std::string_view publisher) {
    const std::wstring expected = widen(publisher);
    const fs::path folder = install_dir_ / update::version_directory_name(version);
    std::error_code error;
    bool any = false;
    for (const auto& entry : fs::recursive_directory_iterator(folder, error)) {
        if (!entry.is_regular_file() || !is_signable(entry.path())) {
            continue;
        }
        const std::wstring signer = trusted_signer_name(entry.path());
        // 自分たちの実行ファイルは発行者の名前まで照合する。同梱する VC++ ランタイムなど
        // 他社が署名した DLL は、信頼できる署名があることだけを求める（署名し直さないため）。
        const bool ours = is_own_executable(entry.path());
        if (signer.empty() || (ours && signer != expected)) {
            return false;
        }
        any = any || ours;
    }
    return !error && any;
}

}  // namespace vocroz::updater::win
