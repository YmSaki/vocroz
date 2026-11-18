// SPDX-License-Identifier: AGPL-3.0-only
//
// YUV4MPEG2 の輝度平面を読み取り、フレーム単位で平均します。
#include "y4m_file.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string_view>

namespace vocroz::tools {
namespace {
int positive(std::string_view s) {
    int n = 0;
    auto end = s.data() + s.size();
    const auto r = std::from_chars(s.data(), end, n);
    return r.ec == std::errc{} && r.ptr == end && n > 0 ? n : 0;
}
common::Result<Y4mVideo, MediaFileError> header(const std::string& line) {
    constexpr std::string_view kSignature = "YUV4MPEG2 ";
    if (!line.starts_with(kSignature))
        return common::make_unexpected(MediaFileError::kInvalidFormat);
    Y4mVideo v;
    std::istringstream fields(line.substr(kSignature.size()));
    for (std::string token; fields >> token;) {
        std::string_view value(token);
        value.remove_prefix(1);
        if (token.front() == 'W')
            v.width = positive(value);
        if (token.front() == 'H')
            v.height = positive(value);
        if (token.front() == 'F') {
            auto split = value.find(':');
            if (split == value.npos || !positive(value.substr(split + 1)))
                return common::make_unexpected(MediaFileError::kInvalidFormat);
            v.fps = static_cast<double>(positive(value.substr(0, split))) /
                    positive(value.substr(split + 1));
        }
        if (token.front() == 'C' && value != "420" && value != "420jpeg" && value != "420mpeg2" &&
            value != "420paldv")
            return common::make_unexpected(MediaFileError::kUnsupportedFormat);
    }
    if (!v.width || !v.height || v.fps <= 0)
        return common::make_unexpected(MediaFileError::kInvalidFormat);
    return v;
}
// フレーム寸法が大きい場合にも一定のバッファで平面を処理します。
constexpr int kBlock = 8192;
}  // namespace
common::Result<void, MediaFileError> write_y4m_flat(const std::filesystem::path& path,
                                                    std::span<const std::uint8_t> lumas, int w,
                                                    int h, int fps) {
    if (w <= 0 || h <= 0 || w % 2 || h % 2 || fps <= 0)
        return common::make_unexpected(MediaFileError::kInvalidFormat);
    std::ofstream file(path, std::ios::binary);
    if (!file)
        return common::make_unexpected(MediaFileError::kCannotOpen);
    file << "YUV4MPEG2 W" << w << " H" << h << " F" << fps << ":1 Ip A1:1 C420jpeg\n";
    const auto plane = [&](long long n, std::uint8_t level) {
        std::vector<char> block(kBlock, static_cast<char>(level));
        while (n > 0 && file) {
            auto chunk = std::min<long long>(kBlock, n);
            file.write(block.data(), chunk);
            n -= chunk;
        }
    };
    const auto pixels = 1LL * w * h;
    for (const auto y : lumas) {
        file << "FRAME\n";
        plane(pixels, y);
        // U と V を無彩色の中点で埋めます。
        plane(pixels / 2, 128);
    }
    file.close();
    if (!file)
        return common::make_unexpected(MediaFileError::kWriteFailed);
    return {};
}
common::Result<Y4mVideo, MediaFileError> read_y4m(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return common::make_unexpected(MediaFileError::kCannotOpen);
    std::string line;
    std::getline(file, line);
    auto parsed = header(line);
    if (!parsed)
        return parsed;
    auto& v = parsed.value();
    const auto pixels = 1LL * v.width * v.height;
    const auto chroma = ((v.width + 1LL) / 2) * ((v.height + 1LL) / 2) * 2;
    std::vector<char> block(kBlock);
    while (std::getline(file, line)) {
        // FRAME の後のパラメーターは平均輝度に影響しません。
        if (line != "FRAME" && !line.starts_with("FRAME "))
            return common::make_unexpected(MediaFileError::kInvalidFormat);
        double sum = 0;
        for (long long left = pixels; left > 0;) {
            auto n = std::min<long long>(kBlock, left);
            if (!file.read(block.data(), n))
                return common::make_unexpected(MediaFileError::kTruncated);
            for (long long i = 0; i < n; ++i)
                sum += static_cast<std::uint8_t>(block[static_cast<std::size_t>(i)]);
            left -= n;
        }
        file.ignore(chroma);
        if (file.gcount() != chroma)
            return common::make_unexpected(MediaFileError::kTruncated);
        v.frame_lumas.push_back(
            static_cast<std::uint8_t>(std::lround(sum / static_cast<double>(pixels))));
    }
    if (!file.eof() || file.bad())
        return common::make_unexpected(MediaFileError::kTruncated);
    return parsed;
}
}  // namespace vocroz::tools
