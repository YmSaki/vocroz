// SPDX-License-Identifier: AGPL-3.0-only
//
// StagedPackage のフェイク。メモリ上のバイト列をパッケージとして扱い、
// 展開は FakeInstallLayout にバージョンを加えるだけにする。
#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "vocroz/update/ports/staged_package.h"

#include "fakes/fake_install_layout.h"

namespace vocroz::update::fakes {

class FakeStagedPackage final : public ports::StagedPackage {
public:
    explicit FakeStagedPackage(FakeInstallLayout& layout) : layout_(layout) {}

    bool read(const ChunkSink& sink) override {
        if (fail_read) {
            return false;
        }
        // 複数のチャンクに分けて渡し、ハッシュの逐次計算を通す。
        constexpr std::size_t kChunk = 7;
        for (std::size_t i = 0; i < bytes.size(); i += kChunk) {
            const std::size_t n = std::min(kChunk, bytes.size() - i);
            sink(std::span<const std::uint8_t>{bytes.data() + i, n});
        }
        return true;
    }

    bool extract(const SemanticVersion& version) override {
        ++extract_calls;
        if (fail_extract) {
            return false;
        }
        layout_.versions.push_back(version);
        return true;
    }

    void discard() override {
        discarded = true;
    }

    std::vector<std::uint8_t> bytes;
    bool fail_read = false;
    bool fail_extract = false;
    bool discarded = false;
    int extract_calls = 0;

private:
    FakeInstallLayout& layout_;
};

}  // namespace vocroz::update::fakes
