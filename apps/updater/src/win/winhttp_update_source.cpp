// SPDX-License-Identifier: AGPL-3.0-only
//
// winhttp_update_source.h の実装。

#include "win/winhttp_update_source.h"

#include <fstream>
#include <functional>
#include <system_error>

#include <windows.h>
#include <winhttp.h>

#include "vocroz/common/version_info.h"

#include "updater_constants.h"
#include "win/win32_text.h"

namespace vocroz::updater::win {
namespace {

namespace fs = std::filesystem;

// WinHTTP のハンドルを RAII で閉じる。
class InternetHandle {
public:
    explicit InternetHandle(HINTERNET handle) : handle_(handle) {}
    ~InternetHandle() {
        if (handle_ != nullptr) {
            ::WinHttpCloseHandle(handle_);
        }
    }
    InternetHandle(const InternetHandle&) = delete;
    InternetHandle& operator=(const InternetHandle&) = delete;

    [[nodiscard]] HINTERNET get() const {
        return handle_;
    }
    explicit operator bool() const {
        return handle_ != nullptr;
    }

private:
    HINTERNET handle_;
};

// 受信したバイト列を受け取る関数。false を返すと受信を中止する。
using BodySink = std::function<bool(const char* data, std::size_t size)>;

// HTTPS で GET し、本文を sink に渡す。HTTP への転送や 200 以外の応答は失敗とする。
[[nodiscard]] bool https_get(const std::string& url_utf8, std::uint64_t max_size,
                             const BodySink& sink) {
    const std::wstring url = widen(url_utf8);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!::WinHttpCrackUrl(url.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS) {
        return false;
    }
    const std::wstring host{parts.lpszHostName, parts.dwHostNameLength};
    std::wstring path{parts.lpszUrlPath, parts.dwUrlPathLength};
    if (parts.lpszExtraInfo != nullptr) {
        path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    }

    const std::wstring agent = widen(common::version_line("pa-updater"));
    const InternetHandle session{::WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session) {
        return false;
    }
    const InternetHandle connection{::WinHttpConnect(session.get(), host.c_str(), parts.nPort, 0)};
    if (!connection) {
        return false;
    }
    const InternetHandle request{
        ::WinHttpOpenRequest(connection.get(), L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                             WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)};
    if (!request) {
        return false;
    }
    // HTTPS から HTTP への転送を拒否する（WinHTTP の既定と同じだが、明示しておく）。
    DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    ::WinHttpSetOption(request.get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirect_policy,
                       sizeof(redirect_policy));
    if (!::WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                              WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !::WinHttpReceiveResponse(request.get(), nullptr)) {
        return false;
    }
    DWORD status = 0;
    DWORD status_size = sizeof(status);
    if (!::WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                               WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
                               WINHTTP_NO_HEADER_INDEX) ||
        status != 200) {
        return false;
    }

    std::uint64_t total = 0;
    char buffer[64 * 1024];
    while (true) {
        DWORD read = 0;
        if (!::WinHttpReadData(request.get(), buffer, sizeof(buffer), &read)) {
            return false;
        }
        if (read == 0) {
            return true;
        }
        total += read;
        // 上限を超える応答は、それ以上受け取らずに失敗とする（ディスクとメモリの保護）。
        if (total > max_size || !sink(buffer, read)) {
            return false;
        }
    }
}

}  // namespace

WinHttpUpdateSource::WinHttpUpdateSource(std::string manifest_url, fs::path updates_dir)
    : manifest_url_(std::move(manifest_url)), updates_dir_(std::move(updates_dir)) {}

std::optional<std::string> WinHttpUpdateSource::fetch_manifest() {
    std::string text;
    const bool ok =
        https_get(manifest_url_, kMaxManifestSize, [&text](const char* data, std::size_t size) {
            text.append(data, size);
            return true;
        });
    if (!ok) {
        return std::nullopt;
    }
    return text;
}

bool WinHttpUpdateSource::download_package(const update::UpdateManifest& manifest) {
    std::error_code error;
    fs::create_directories(updates_dir_, error);
    if (error) {
        return false;
    }
    const fs::path target = updates_dir_ / kStagedPackageFileName;
    fs::path partial = target;
    partial += L".partial";
    bool ok = false;
    {
        std::ofstream file(partial, std::ios::binary | std::ios::trunc);
        if (!file) {
            return false;
        }
        ok = https_get(manifest.package_url, manifest.package_size,
                       [&file](const char* data, std::size_t size) {
                           file.write(data, static_cast<std::streamsize>(size));
                           return static_cast<bool>(file);
                       });
        file.flush();
        ok = ok && static_cast<bool>(file);
    }
    // 途中までのファイルを package.zip の名前で残さない。--apply が読むのは完成したファイルだけ。
    if (!ok) {
        fs::remove(partial, error);
        return false;
    }
    fs::rename(partial, target, error);
    return !error;
}

bool WinHttpUpdateSource::store_manifest(std::string_view text) {
    std::ofstream file(updates_dir_ / kStagedManifestFileName, std::ios::binary | std::ios::trunc);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.flush();
    return static_cast<bool>(file);
}

}  // namespace vocroz::updater::win
