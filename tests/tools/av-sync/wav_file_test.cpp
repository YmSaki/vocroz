// SPDX-License-Identifier: AGPL-3.0-only
//
// 試験パターンの生成と計測の契約を検証します。
#include "wav_file.h"

#include <fstream>

#include <gtest/gtest.h>

#include "wav_file.h"

namespace vocroz::tools {
namespace {
class WavFileTest : public ::testing::Test {
protected:
    std::filesystem::path dir = std::filesystem::temp_directory_path() /
                                (std::string("vocroz-WavFileTest-") +
                                 ::testing::UnitTest::GetInstance()->current_test_info()->name());
    WavFileTest() {
        std::filesystem::create_directories(dir);
    }
    ~WavFileTest() override {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
    void put(std::string_view bytes) {
        std::ofstream f(dir / "file", std::ios::binary);
        f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
};
// PCM の量子化と途中で切れたデータを検査します。
TEST_F(WavFileTest, ReadsMonoAndFileErrors) {
    auto path = dir / "file";
    EXPECT_EQ(read_wav(path).error(), MediaFileError::kCannotOpen);
    put("bad");
    EXPECT_EQ(read_wav(path).error(), MediaFileError::kInvalidFormat);
    const float a[]{-1, 0.3F, 1};
    ASSERT_TRUE(write_wav_mono16(path, a, 48000));
    auto r = read_wav(path);
    ASSERT_TRUE(r);
    EXPECT_EQ(r.value().sample_rate, 48000);
    ASSERT_EQ(r.value().samples.size(), 3U);
    for (size_t i = 0; i < 3; ++i)
        EXPECT_NEAR(a[i], r.value().samples[i], 1.0 / 32767);
    std::filesystem::resize_file(path, 48);
    EXPECT_EQ(read_wav(path).error(), MediaFileError::kTruncated);
}
// 24 ビット PCM と浮動小数点の先頭チャンネルを読みます。
TEST_F(WavFileTest, ReadsWideStereo) {
    for (int bits : {24, 32}) {
        std::string data = "RIFF";
        auto le = [&](unsigned n, int count) {
            while (count--) {
                data.push_back(static_cast<char>(n & 255));
                n >>= 8;
            }
        };
        unsigned stride = static_cast<unsigned>(bits / 4);
        le(36 + stride, 4);
        data += "WAVEfmt ";
        le(16, 4);
        le(bits == 24 ? 1 : 3, 2);
        le(2, 2);
        le(48000, 4);
        le(48000 * stride, 4);
        le(stride, 2);
        le(static_cast<unsigned>(bits), 2);
        data += "data";
        le(stride, 4);
        le(bits == 24 ? 0x400000 : 0x3f000000, bits / 8);
        le(0, bits / 8);
        put(data);
        auto r = read_wav(dir / "file");
        ASSERT_TRUE(r);
        ASSERT_EQ(r.value().samples.size(), 1U);
        EXPECT_NEAR(r.value().samples.front(), 0.5, 0.00001);
    }
}
}  // namespace
}  // namespace vocroz::tools
