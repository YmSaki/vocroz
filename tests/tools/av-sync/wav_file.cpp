// SPDX-License-Identifier: AGPL-3.0-only
//
// RIFF のチャンクを検査し、先頭チャンネルの音声を読み取ります。
#include "wav_file.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <string_view>

namespace vocroz::tools {
namespace {
using Bytes = std::span<const char>;
// WAV のサイズとサンプルはリトルエンディアンです。
constexpr unsigned kByteBits = 8;
// 16 ビット PCM の正の最大振幅。
constexpr int kPcmScale = 32767;
// WAVE_FORMAT_IEEE_FLOAT の形式番号。
constexpr unsigned kFloat = 3;
// WAVE_FORMAT_EXTENSIBLE の形式番号。
constexpr unsigned kExtensible = 0xfffe;
// 書き出しに使う PCM のビット数。
constexpr unsigned kPcmBits = 16;
// 読み込みに対応する広い PCM のビット数。
constexpr unsigned kWideBits = 24;
// IEEE 単精度のビット数。
constexpr unsigned kFloatBits = 32;
// モノラル PCM の RIFF サイズに含まれるデータ以外のバイト数。
constexpr unsigned kRiffOverhead = 36;
std::uint32_t number(Bytes bytes) {
    std::uint32_t result = 0;
    for (auto it = bytes.rbegin(); it != bytes.rend(); ++it)
        result = (result << kByteBits) | static_cast<std::uint8_t>(*it);
    return result;
}
void append(std::vector<char>& bytes, std::uint32_t value, unsigned count) {
    for (unsigned i = 0; i < count; ++i) {
        bytes.push_back(static_cast<char>(static_cast<std::uint8_t>(value)));
        value >>= kByteBits;
    }
}
struct Format {
    unsigned encoding, rate, stride, bits;
};
common::Result<Format, MediaFileError> format(Bytes fmt) {
    if (fmt.size() < 16)
        return common::make_unexpected(MediaFileError::kInvalidFormat);
    Format f{number(fmt.first(2)), number(fmt.subspan(4, 4)), number(fmt.subspan(12, 2)),
             number(fmt.subspan(14, 2))};
    if (f.encoding == kExtensible) {
        if (fmt.size() < 40 || number(fmt.subspan(16, 2)) < 22)
            return common::make_unexpected(MediaFileError::kInvalidFormat);
        constexpr std::array<unsigned char, 12> kGuidTail{0, 0,    0x10, 0,    0x80, 0,
                                                          0, 0xaa, 0,    0x38, 0x9b, 0x71};
        if (!std::equal(kGuidTail.begin(), kGuidTail.end(), fmt.begin() + 28,
                        [](unsigned char a, char b) { return a == static_cast<unsigned char>(b); }))
            return common::make_unexpected(MediaFileError::kUnsupportedFormat);
        f.encoding = number(fmt.subspan(24, 4));
    }
    if (!(f.encoding == 1 && (f.bits == kPcmBits || f.bits == kWideBits)) &&
        !(f.encoding == kFloat && f.bits == kFloatBits))
        return common::make_unexpected(MediaFileError::kUnsupportedFormat);
    if (!f.rate || f.rate > std::numeric_limits<int>::max() || !f.stride ||
        f.stride != number(fmt.subspan(2, 2)) * (f.bits / kByteBits))
        return common::make_unexpected(MediaFileError::kInvalidFormat);
    return f;
}
float sample(std::uint32_t raw, const Format& f) {
    if (f.encoding == kFloat)
        return std::clamp(std::bit_cast<float>(raw), -1.0F, 1.0F);
    const auto sign = 1U << (f.bits - 1);
    const auto value = static_cast<int>(raw ^ sign) - static_cast<int>(sign);
    return std::max(-1.0F, static_cast<float>(value) / static_cast<float>(sign - 1));
}
}  // namespace
common::Result<void, MediaFileError> write_wav_mono16(const std::filesystem::path& path,
                                                      std::span<const float> samples, int rate) {
    constexpr auto kMaxSize = std::numeric_limits<std::uint32_t>::max();
    if (rate <= 0 || samples.size() > (kMaxSize - kRiffOverhead) / 2)
        return common::make_unexpected(MediaFileError::kInvalidFormat);
    std::ofstream file(path, std::ios::binary);
    if (!file)
        return common::make_unexpected(MediaFileError::kCannotOpen);
    std::vector<char> bytes;
    const auto tag = [&](std::string_view s) { bytes.insert(bytes.end(), s.begin(), s.end()); };
    const auto size = static_cast<std::uint32_t>(samples.size() * 2);
    tag("RIFF");
    append(bytes, kRiffOverhead + size, 4);
    tag("WAVEfmt ");
    append(bytes, 16, 4);
    append(bytes, 1, 2);
    append(bytes, 1, 2);
    append(bytes, static_cast<unsigned>(rate), 4);
    append(bytes, static_cast<unsigned>(rate) * 2, 4);
    append(bytes, 2, 2);
    append(bytes, kPcmBits, 2);
    tag("data");
    append(bytes, size, 4);
    // 最近傍の整数へ丸め、最大振幅を 16 ビット PCM に収めます。
    for (const float x : samples)
        append(bytes, static_cast<unsigned>(std::lround(std::clamp(x, -1.0F, 1.0F) * kPcmScale)),
               2);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    file.close();
    if (!file)
        return common::make_unexpected(MediaFileError::kWriteFailed);
    return {};
}
common::Result<WavAudio, MediaFileError> read_wav(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return common::make_unexpected(MediaFileError::kCannotOpen);
    std::vector<char> buffer(std::istreambuf_iterator<char>{file}, {});
    const Bytes bytes(buffer);
    if (bytes.size() < 12 || std::string_view(bytes.data(), 4) != "RIFF" ||
        std::string_view(bytes.data() + 8, 4) != "WAVE")
        return common::make_unexpected(MediaFileError::kInvalidFormat);
    const auto end = 8ULL + number(bytes.subspan(4, 4));
    if (end > bytes.size())
        return common::make_unexpected(MediaFileError::kTruncated);
    Bytes fmt, data;
    for (std::size_t pos = 12; pos < end;) {
        if (end - pos < 8)
            return common::make_unexpected(MediaFileError::kTruncated);
        const std::string_view tag(bytes.data() + pos, 4);
        const auto size = number(bytes.subspan(pos + 4, 4));
        pos += 8;
        // 奇数長のチャンクの後には、長さに含まれない詰め物が 1 バイトあります。
        const auto padded = static_cast<std::size_t>(size) + (size % 2);
        if (padded > end - pos)
            return common::make_unexpected(MediaFileError::kTruncated);
        if (tag == "fmt ")
            fmt = bytes.subspan(pos, size);
        if (tag == "data")
            data = bytes.subspan(pos, size);
        pos += padded;
    }
    auto parsed = format(fmt);
    if (!parsed)
        return common::make_unexpected(parsed.error());
    const auto f = parsed.value();
    if (!data.data() || data.size() % f.stride)
        return common::make_unexpected(MediaFileError::kInvalidFormat);
    WavAudio result{static_cast<int>(f.rate), {}};
    for (std::size_t i = 0; i < data.size(); i += f.stride)
        result.samples.push_back(sample(number(data.subspan(i, f.bits / kByteBits)), f));
    return result;
}
}  // namespace vocroz::tools
