// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト用の SecretStore。メモリに秘密情報を持ち、読み書きの失敗も再現できる。
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <vector>

#include "vocroz/session/ports/secret_store.h"

namespace vocroz::session {

/// メモリに秘密情報を持つ SecretStore。値がなければ kNotFound を返す。
class FakeSecretStore final : public SecretStore {
public:
    common::Result<std::vector<std::uint8_t>, SecretError> load(SecretSlot slot) override {
        if (load_error_) {
            return common::make_unexpected(*load_error_);
        }
        const auto it = values_.find(slot);
        if (it == values_.end()) {
            return common::make_unexpected(SecretError::kNotFound);
        }
        return it->second;
    }

    common::Result<void, SecretError> save(SecretSlot slot,
                                           std::span<const std::uint8_t> value) override {
        if (save_error_) {
            return common::make_unexpected(*save_error_);
        }
        values_[slot].assign(value.begin(), value.end());
        ++save_count_;
        return {};
    }

    common::Result<void, SecretError> erase(SecretSlot slot) override {
        values_.erase(slot);
        return {};
    }

    /// 保存されている値を直接置く（前回の起動で保存した状態を作る）。
    void put(SecretSlot slot, std::vector<std::uint8_t> value) {
        values_[slot] = std::move(value);
    }
    /// 保存されている値。なければ std::nullopt。
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> stored(SecretSlot slot) const {
        const auto it = values_.find(slot);
        return it == values_.end() ? std::nullopt : std::optional{it->second};
    }
    /// 以降の load() を error で失敗させる。std::nullopt で戻す。
    void fail_load(std::optional<SecretError> error) {
        load_error_ = error;
    }
    /// 以降の save() を error で失敗させる。std::nullopt で戻す。
    void fail_save(std::optional<SecretError> error) {
        save_error_ = error;
    }
    [[nodiscard]] int save_count() const {
        return save_count_;
    }

private:
    std::map<SecretSlot, std::vector<std::uint8_t>> values_;
    std::optional<SecretError> load_error_;
    std::optional<SecretError> save_error_;
    int save_count_ = 0;
};

}  // namespace vocroz::session
