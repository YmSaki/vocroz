// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト用の PluginLoader と PluginInstance。読み込みの成功と失敗をテストが決め、読み込んだ
// インスタンスがいくつ生きているかを数えて、破棄の時機を確かめられるようにする。
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "vocroz/audio_core/ports/plugin_processor.h"
#include "vocroz/session/ports/plugin_loader.h"

namespace vocroz::session {

/// 決まった処理を返すインスタンス。状態はバイト列をそのまま覚える。
class FakePluginInstance final : public PluginInstance {
public:
    /// @param processor 返す処理
    /// @param alive     生きているインスタンスの数（作るときに増やし、破棄するときに減らす）
    FakePluginInstance(std::shared_ptr<audio_core::PluginProcessor> processor,
                       std::shared_ptr<int> alive)
        : processor_(std::move(processor)), alive_(std::move(alive)) {
        ++*alive_;
    }
    ~FakePluginInstance() override {
        --*alive_;
    }
    FakePluginInstance(const FakePluginInstance&) = delete;
    FakePluginInstance& operator=(const FakePluginInstance&) = delete;

    [[nodiscard]] std::shared_ptr<audio_core::PluginProcessor> processor() const override {
        return processor_;
    }

    [[nodiscard]] common::Result<std::vector<std::uint8_t>, PluginStateError> save_state()
        override {
        return state_;
    }

    [[nodiscard]] common::Result<void, PluginStateError> restore_state(
        std::span<const std::uint8_t> state) override {
        state_.assign(state.begin(), state.end());
        return {};
    }

private:
    std::shared_ptr<audio_core::PluginProcessor> processor_;
    std::shared_ptr<int> alive_;
    std::vector<std::uint8_t> state_;
};

/// 読み込みを記録し、FakePluginInstance を返す PluginLoader。
class FakePluginLoader final : public PluginLoader {
public:
    /// @param make_processor 読み込みごとに処理を作る関数
    explicit FakePluginLoader(
        std::function<std::shared_ptr<audio_core::PluginProcessor>()> make_processor)
        : make_processor_(std::move(make_processor)) {}

    [[nodiscard]] common::Result<std::unique_ptr<PluginInstance>, PluginLoadError> load(
        const PluginDescription& description) override {
        loaded_.push_back(description.identifier);
        if (fail_with_) {
            return common::make_unexpected(*fail_with_);
        }
        return std::unique_ptr<PluginInstance>{
            std::make_unique<FakePluginInstance>(make_processor_(), alive_)};
    }

    /// 以降の読み込みを error で失敗させる。
    void fail_with(PluginLoadError error) noexcept {
        fail_with_ = error;
    }

    /// 読み込んだプラグインの識別子（古い順）。
    [[nodiscard]] const std::vector<std::string>& loaded() const noexcept {
        return loaded_;
    }

    /// 生きているインスタンスの数。
    [[nodiscard]] int alive() const noexcept {
        return *alive_;
    }

private:
    std::function<std::shared_ptr<audio_core::PluginProcessor>()> make_processor_;
    std::optional<PluginLoadError> fail_with_;
    std::vector<std::string> loaded_;
    std::shared_ptr<int> alive_ = std::make_shared<int>(0);
};

}  // namespace vocroz::session
