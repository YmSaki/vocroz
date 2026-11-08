// SPDX-License-Identifier: AGPL-3.0-only
//
// ポインターファイルとバージョン別フォルダの名前の単体テスト。

#include "vocroz/update/install_pointer.h"

#include <gtest/gtest.h>

namespace vocroz::update {
namespace {

const SemanticVersion kA{0, 1, 0, "alpha.1"};
const SemanticVersion kB{0, 1, 0, "alpha.2"};

TEST(InstallPointerTest, RoundTripsWithAndWithoutPrevious) {
    for (const InstallPointer& pointer :
         {InstallPointer{kB, kA}, InstallPointer{kB, std::nullopt}}) {
        EXPECT_EQ(parse_install_pointer(serialize_install_pointer(pointer)), pointer);
    }
    EXPECT_EQ(serialize_install_pointer({kB, kA}),
              "current=0.1.0-alpha.2\nprevious=0.1.0-alpha.1\n");
}

TEST(InstallPointerTest, TreatsBrokenFileAsMissing) {
    for (const std::string_view text :
         {"", "current=0.1.0", "current=bad\n", "previous=0.1.0\n", "current=0.1.0\nx=1\n",
          "current=0.1.0\nprevious=0.0.1\nprevious=0.0.1\n"}) {
        EXPECT_FALSE(parse_install_pointer(text).has_value()) << text;
    }
}

TEST(VersionDirectoryNameTest, RoundTripsAndIgnoresOtherFolders) {
    EXPECT_EQ(version_directory_name(kB), "app-0.1.0-alpha.2");
    EXPECT_EQ(parse_version_directory_name("app-0.1.0-alpha.2"), kB);
    EXPECT_FALSE(parse_version_directory_name("app-").has_value());
    EXPECT_FALSE(parse_version_directory_name("staging-0.1.0").has_value());
    EXPECT_FALSE(parse_version_directory_name("app-0.1.0.tmp").has_value());
}

}  // namespace
}  // namespace vocroz::update
