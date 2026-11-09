// SPDX-License-Identifier: AGPL-3.0-only
//
// write_diagnostic_report() の実装。

#include "vocroz/persistence/diagnostic_report.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <optional>
#include <ranges>
#include <string_view>
#include <system_error>

#include "atomic_file.h"
#include "detail/durable_io.h"
#include "detail/time_format.h"
#include "persistence_constants.h"
#include "secret_masker.h"
#include "settings_codec.h"

namespace vocroz::persistence {
namespace {

constexpr std::string_view kMaskedName = "<masked>";

// 表示名とセッション名は自由な文字列で、形からは伏せられないので値ごと伏せる。
// 空かどうかは調査に役立つので、空のときは空のまま残す。
void mask_free_text(std::string& text) {
    if (!text.empty()) {
        text = kMaskedName;
    }
}

template <typename Settings, typename Decode, typename Encode, typename Mask>
std::string describe_settings(const std::filesystem::path& path, Decode decode, Encode encode,
                              Mask mask) {
    const AtomicFile file{path};
    const auto current = file.read_current();
    if (!current) {
        return "（読めませんでした）\n";
    }
    if (!current.value()) {
        return "（ありません）\n";
    }
    auto settings = decode(*current.value());
    if (!settings) {
        const auto backup = file.read_backup();
        const bool backup_ok = backup && backup.value() && decode(*backup.value()).has_value();
        return backup_ok ? "（壊れています。バックアップは正常です）\n"
                         : "（壊れています。正常なバックアップはありません）\n";
    }
    mask(*settings);
    return mask_sensitive_text(encode(*settings));
}

struct LogFile {
    std::filesystem::path path;
    std::filesystem::file_time_type modified;
    std::uintmax_t size = 0;
};

// *.log を新しい順に並べる。
std::vector<LogFile> list_logs(const std::filesystem::path& directory) {
    std::vector<LogFile> logs;
    std::error_code ec;
    for (std::filesystem::directory_iterator it{directory, ec}, end; !ec && it != end;
         it.increment(ec)) {
        if (!it->is_regular_file(ec) || it->path().extension() != ".log") {
            continue;
        }
        LogFile log{it->path(), it->last_write_time(ec), it->file_size(ec)};
        if (!ec) {
            logs.push_back(std::move(log));
        }
    }
    std::sort(logs.begin(), logs.end(),
              [](const LogFile& a, const LogFile& b) { return a.modified > b.modified; });
    return logs;
}

// ファイルの末尾から limit バイトまでを読む。新しいログほど末尾にあるため。
std::optional<std::string> read_tail(const std::filesystem::path& path, std::uintmax_t size,
                                     std::uintmax_t limit) {
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        return std::nullopt;
    }
    if (size > limit) {
        in.seekg(static_cast<std::streamoff>(size - limit));
    }
    std::string content{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    if (in.bad()) {
        return std::nullopt;
    }
    return content;
}

std::string describe_logs(const std::filesystem::path& directory) {
    const auto logs = list_logs(directory);
    // 新しいログから上限まで選び、読みやすいよう古い順に並べ直して書く。
    std::vector<std::string> sections;
    std::uintmax_t remaining = kMaxReportLogBytes;
    for (const auto& log : logs) {
        if (remaining == 0) {
            break;
        }
        std::string section = "== ログ: " + log.path.filename().string() + " ==\n";
        if (const auto content = read_tail(log.path, log.size, remaining)) {
            if (log.size > remaining) {
                section += "（大きさの上限のため、末尾だけを入れています）\n";
            }
            remaining -= std::min(remaining, static_cast<std::uintmax_t>(content->size()));
            section += mask_sensitive_text(*content);
        } else {
            section += "（読めませんでした）\n";
        }
        if (section.back() != '\n') {
            section += '\n';
        }
        sections.push_back(std::move(section));
    }
    if (sections.empty()) {
        return "== ログ ==\n（ありません）\n";
    }
    std::string out;
    for (const auto& section : std::views::reverse(sections)) {
        out += section;
    }
    return out;
}

}  // namespace

common::Result<void, DiagnosticReportError> write_diagnostic_report(
    const DiagnosticReportInput& input, const std::filesystem::path& output) {
    std::string report = "VOCROZ 診断レポート\n";
    report += "作成: " + detail::format_utc_timestamp(input.created_at) + "\n";
    report += "バージョン: " + mask_sensitive_text(input.product_version) + "\n";
    report += "== 環境 ==\n";
    for (const auto& line : input.environment) {
        report += mask_sensitive_text(line) + "\n";
    }
    report += "== 親機の設定 ==\n";
    report += describe_settings<session::HostSettings>(
        input.data_dir / kHostSettingsFileName, decode_host_settings, encode_host_settings,
        [](session::HostSettings& s) {
            // バスと出力の名前もオペレーターが自由に付けるので、人名が入りうる。
            mask_free_text(s.session_name);
            for (auto& bus : s.routing.buses) {
                mask_free_text(bus.name);
            }
            for (auto& extra_output : s.routing.outputs) {
                mask_free_text(extra_output.name);
            }
        });
    report += "== 子機の設定 ==\n";
    report += describe_settings<session::ClientSettings>(
        input.data_dir / kClientSettingsFileName, decode_client_settings, encode_client_settings,
        [](session::ClientSettings& s) { mask_free_text(s.display_name); });
    report += describe_logs(input.log_dir);

    if (!detail::write_file_durably(output, report)) {
        return common::make_unexpected(DiagnosticReportError::kWriteFailed);
    }
    return {};
}

}  // namespace vocroz::persistence
