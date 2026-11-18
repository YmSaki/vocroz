// SPDX-License-Identifier: AGPL-3.0-only
//
// 試験パターンの生成と計測の契約を検証します。
#include "y4m_file.h"

#include <fstream>

#include <gtest/gtest.h>

#include "y4m_file.h"
namespace vocroz::tools {
namespace {
class Y4mFileTest : public ::testing::Test {
protected:
    std::filesystem::path dir = std::filesystem::temp_directory_path() /
                                (std::string("vocroz-Y4mFileTest-") +
                                 ::testing::UnitTest::GetInstance()->current_test_info()->name());
    Y4mFileTest() {
        std::filesystem::create_directories(dir);
    }
    ~Y4mFileTest() override {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
    void put(std::string_view bytes) {
        std::ofstream f(dir / "file", std::ios::binary);
        f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
};
// 輝度の往復と不正なヘッダーや末尾を検査します。
TEST_F(Y4mFileTest, ReadsFramesAndRejectsBrokenFiles) {
    auto path = dir / "file";
    std::vector<std::uint8_t> levels{1, 44, 255};
    ASSERT_TRUE(write_y4m_flat(path, levels, 4, 2, 60));
    auto r = read_y4m(path);
    ASSERT_TRUE(r);
    EXPECT_EQ(r.value().width, 4);
    EXPECT_EQ(r.value().height, 2);
    EXPECT_NEAR(r.value().fps, 60, 0.001);
    EXPECT_EQ(r.value().frame_lumas, levels);
    std::filesystem::resize_file(path, std::filesystem::file_size(path) - 1);
    EXPECT_EQ(read_y4m(path).error(), MediaFileError::kTruncated);
    put("bad");
    EXPECT_EQ(read_y4m(path).error(), MediaFileError::kInvalidFormat);
    put("YUV4MPEG2 W4 H2 F60:1 C444\n");
    EXPECT_EQ(read_y4m(path).error(), MediaFileError::kUnsupportedFormat);
}
}  // namespace
}  // namespace vocroz::tools
